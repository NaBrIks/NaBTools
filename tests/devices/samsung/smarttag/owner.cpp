// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Oleg Kozhevnikov
// Windows CNG is an independent host crypto provider. Firmware uses mbedTLS.
#include "NaBTools/devices/samsung/smarttag/smarttag.hpp"
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <deque>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace NaBTools::runtime;
void check(bool v, const char *m)
{
	if(!v)
		throw std::runtime_error(m);
}

std::array<uint8_t, 16> hex(const char *s)
{
	std::array<uint8_t, 16> out{}; for(unsigned i = 0; i < 16; ++i)
	{
		unsigned v; std::sscanf(s + i * 2, "%2x", &v); out[i] = uint8_t(v);
	}
	return out;
}

SmartTagCrypto crypto()
{
	return {nullptr,
	        [](void *, std::span<uint8_t> out){
				for(size_t i = 0; i < out.size(); ++i)
					out[i] = uint8_t(i + 16);
				return true;
			},
	        [](void *, Bytes in, std::span<uint8_t, 32> out){
				BCRYPT_ALG_HANDLE alg{}; if(BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
					return false;

				const auto rc = BCryptHash(alg, nullptr, 0, const_cast<PUCHAR>(in.data()), ULONG(in.size()), out.data(),
				        32); BCryptCloseAlgorithmProvider(alg, 0); return rc >= 0;
			},
	        [](void *, bool enc, Bytes key, Bytes iv, Bytes in, std::span<uint8_t> out){
				BCRYPT_ALG_HANDLE alg{}; BCRYPT_KEY_HANDLE handle{};
				if(key.size() != 16 || iv.size() != 16 || in.size() != out.size()) return false;

				if(BCryptOpenAlgorithmProvider(&alg, BCRYPT_AES_ALGORITHM, nullptr, 0) < 0) return false;

				auto rc = BCryptSetProperty(alg, BCRYPT_CHAINING_MODE,
				        reinterpret_cast<PUCHAR>(const_cast<wchar_t *>(BCRYPT_CHAIN_MODE_CBC)),
				        sizeof(BCRYPT_CHAIN_MODE_CBC), 0);
				if(rc >= 0)
					rc = BCryptGenerateSymmetricKey(alg, &handle, nullptr, 0, const_cast<PUCHAR>(key.data()), 16, 0);

				std::array<uint8_t, 16> working_iv; std::copy(iv.begin(), iv.end(), working_iv.begin()); ULONG size{};
				if(rc >= 0)
					rc = (enc ? BCryptEncrypt : BCryptDecrypt)(handle, const_cast<PUCHAR>(in.data()), ULONG(in.size()),
					        nullptr, working_iv.data(), 16, out.data(), ULONG(out.size()), &size, 0);

				if(handle)
					BCryptDestroyKey(handle);

				BCryptCloseAlgorithmProvider(alg, 0); return rc >= 0 && size == out.size();
			}};
}

struct Client : IGattClient {
	std::deque<GattEvent> events; uint16_t last_handle{}; std::vector<uint8_t> last_value;
	unsigned subscriptions{}, writes{}, disconnects{}; bool reject{};
	GattStats stats()const override
	{
		return {true, 0, 0};
	}

	Status connect(DeviceId, Address, uint64_t, GattSessionId &s)override
	{
		s = {0, 7}; return Status::ok;
	}

	Status disconnect(GattSessionId, uint64_t)override
	{
		++disconnects; return Status::ok;
	}

	Status discover(GattSessionId, std::optional<Uuid>, uint64_t)override
	{
		return Status::ok;
	}

	Status subscribe(GattSessionId, uint16_t h, uint16_t c, bool i, uint64_t)override
	{
		check(c == h + 1 && i, "CCCD / indication"); last_handle = h; ++subscriptions; return Status::ok;
	}

	Status read(GattSessionId, uint16_t, uint64_t)override
	{
		return Status::ok;
	}

	Status write(GattSessionId, uint16_t h, Bytes value, bool response, uint64_t)override
	{
		check(response, "writes request ATT completion"); last_handle = h;
		last_value.assign(value.begin(), value.end()); ++writes; return reject ? Status::busy : Status::ok;
	}

	void cancel(DeviceId)override
	{
	}

	bool take(GattEvent &e)override
	{
		if(events.empty())
			return false;

		e = events.front(); events.pop_front(); return true;
	}

};
struct Backend : IBackend {Client client; Status start()override
						   {
							   return Status::ok;
						   }

	                       void pump()override
						   {
						   }

	                       Status scan(bool)override
						   {
							   return Status::ok;
						   }

	                       bool take(Advertisement &)override
						   {
							   return false;
						   }

	                       BackendSnapshot snapshot()const override
						   {
							   return {};
						   }

	                       IGattClient * gatt()override
						   {
							   return &client;
						   }
};
Uuid uuid(const char *s)
{
	Uuid u; unsigned n = 0, v = 0; for(; *s; ++s)
	{
		if(*s == '-')
			continue;

		v = (v << 4) | (*s <= '9' ? *s - '0' : *s - 'a' + 10); if(n++ % 2)
			u.bytes[16 - n / 2] = uint8_t(v);
	}
	return u;
}

int main()
{
	try{
		const auto master = hex("000102030405060708090a0b0c0d0e0f");
		const auto tag = hex("202122232425262728292a2b2c2d2e2f");
		const auto expected = hex("d19bd62370e4ee1ceaf8571a7ac088a3");
		const auto peer = hex("32c7f4d27b99fad38c322b137c1b42dd");
		const auto press = hex("b41b4bc54182aabe628836bde17983b5");
		const auto hold = hex("c1ee050ce80696beaa65310f66db9d69");
		SmartTagOwnerSession session; std::array<uint8_t, 16> out{}; uint32_t counter{}; uint8_t action{};
		check(!session.command(1, {}, out), "no command before auth"); check(session.begin(crypto(), master), "begin");
		check(session.challenge(tag, out) && out == expected, "SHA256 KDF + CBC PKCS7 challenge independent vector");
		check(!session.authenticated(), "challenge is not peer proof");
		check(session.verify(peer) && session.authenticated(), "mutual proof");
		check(session.command(1, {}, out) && out == press, "first command counter is 1, LE, encrypted");
		check(session.button(press, counter, action) && counter == 1 && action == 1, "decode encrypted press");
		check(!session.button(press, counter, action), "replay rejected");
		check(!session.button(hex("0ec3de21f1b9cc9b13ab4081693ccd06"), counter, action),
		    "invalid padding rejected without consuming RX counter");
		check(session.button(hold, counter, action) && counter == 2 && action == 2, "hold after rejected ciphertext");
		session.reset(); check(!session.button(press, counter, action), "disconnect discards keys");
		check(session.begin(crypto(), master) && session.challenge(tag, out), "restart"); auto wrong = peer;
		wrong[7] ^= 1;
		check(!session.verify(wrong) && !session.authenticated(), "wrong peer proof invalidates session");
		check(session.begin(crypto(), master) && !session.challenge(Bytes(tag.data(), 15), out),
		    "short nonce rejected");
		check(session.begin(crypto(), master) && session.challenge(tag,
		    out) && session.verify(hex("5202523b8c811bef135a7a78ce61b5e6")),
		    "EI-T5300 NUL-terminated proof accepted exactly");
		auto backend = std::make_unique<Backend>(); auto *b = backend.get(); BtEngine engine(std::move(backend));
		engine.start();
		struct Capture {unsigned buttons{}, cbor_calls{}; bool remove{}; BtEngine *engine; std::vector<uint8_t> cbor;
		} capture{0, 0, false, &engine};
		SmartTagActions actions;
		actions.button = {&capture, [](void *p, const MessageInfo &meta, const SmartTagButtonReading &reading){
							  auto &c = *static_cast<Capture *>(p); check(reading.action == 1, "typed press");
							  ++c.buttons; if(c.remove)
								  c.engine->remove_device(meta.device);

							  return Delivery::accepted;
						  }};
		actions.cbor = {&capture, [](void *p, const MessageInfo &, Bytes bytes){
							auto &c = *static_cast<Capture *>(p); ++c.cbor_calls;
							c.cbor.assign(bytes.begin(), bytes.end()); return Delivery::accepted;
						}};
		SmartTagOwnerAccess access{crypto(), nullptr, [](void *, DeviceId, Address, std::span<uint8_t, 16> m){
									   for(unsigned i = 0; i < 16; ++i)
										   m[i] = uint8_t(i);
									   return true;
								   }};
		SmartTagModule module(actions, access); auto id = module.add_known(engine, {});
		auto invoke = [&](const char *key, uint64_t req){
				return engine.invoke(id, {key, "", {}, req, 100});
			};
		auto push = [&](GattEventKind kind, uint16_t h, uint64_t request, Bytes bytes = Bytes{}){
				GattEvent e; e.kind = kind; e.handle = h; e.owner = id; e.session = {0, 7}; e.request_id = request;
				e.size = uint16_t(bytes.size()); std::copy(bytes.begin(), bytes.end(), e.data.begin());
				b->client.events.push_back(e); engine.poll();
			};
		auto discover = [&](const char *text, uint16_t h){
				GattEvent e; e.kind = GattEventKind::characteristic; e.handle = h; e.uuid = uuid(text); e.owner = id;
				e.session = {0, 7}; e.request_id = 1; e.properties = 0x28; b->client.events.push_back(e);
				auto d = e; d.kind = GattEventKind::descriptor; d.uuid = Uuid::from16(0x2902); d.parent_handle = h;
				d.handle = h + 1; b->client.events.push_back(d); engine.poll();
			};
		auto connect = [&]{
				check(invoke("connection.connect", 1) == Status::ok, "connect"); push(GattEventKind::connected, 0, 1);
				discover("dee30001-182d-5496-b1ad-14f216324184", 30);
				discover("dee30003-182d-5496-b1ad-14f216324184", 40);
				discover("a12be31c-5b38-4773-9b9d-3d5735233a7c", 50);
				discover("4ebe81f6-b952-465e-9ece-5ca39d4e8955", 60); push(GattEventKind::discovery_complete, 0, 1);
			};
		auto authenticate = [&](bool early, bool nul = false){
				const auto actual_peer = nul ? hex("5202523b8c811bef135a7a78ce61b5e6") : peer;
				check(invoke("owner.authenticate", 2) == Status::ok, "authenticate accepted");
				push(GattEventKind::subscribed, 50, 2); push(GattEventKind::subscribed, 60, 2);
				check(b->client.last_handle == 50, "nonce written after both subscriptions");
				const auto before = b->client.writes;
				if(early)
				{
					push(GattEventKind::notification, 50, 0, tag);
					check(b->client.writes == before, "wait for nonce write ACK");
				}

				push(GattEventKind::write, 50, 2); if(!early) push(GattEventKind::notification, 50, 0, tag);

				check(b->client.last_handle == 60 && b->client.last_value == std::vector<uint8_t>(expected.begin(),
				    expected.end()), "proof write uses module crypto");
				if(early) push(GattEventKind::notification, 60, 0, actual_peer);

				check(invoke("alert.start", 7) != Status::ok, "no command until handshake writes completed");
				push(GattEventKind::write, 60, 2); if(!early) push(GattEventKind::notification, 60, 0, actual_peer);
			};
		connect(); authenticate(true); check(invoke("alert.start", 3) == Status::ok, "authenticated ring");
		check(b->client.last_handle == 30 && b->client.last_value == std::vector<uint8_t>(press.begin(), press.end()),
		    "ring ciphertext / discovered handle"); push(GattEventKind::write, 30, 3);
		check(invoke("button.observe", 4) == Status::ok, "subscribe decoded button");
		push(GattEventKind::subscribed, 40, 4); push(GattEventKind::write, 40, 4); push(GattEventKind::write, 40, 4);
		push(GattEventKind::notification, 40, 0, press); check(capture.buttons == 1, "typed decoded press");
		push(GattEventKind::notification, 40, 0, press); check(capture.buttons == 1, "duplicate press not repeated");
		push(GattEventKind::disconnected, 0, 4);
		check(invoke("alert.start", 5) == Status::unsupported, "no keys after disconnect");
		connect(); authenticate(false, true); b->client.reject = true; const auto disconnects = b->client.disconnects;
		check(invoke("alert.stop", 6) == Status::busy && b->client.disconnects == disconnects + 1,
		    "ambiguous write cancels encrypted session");
		check(invoke("alert.start", 7) != Status::ok, "cannot reuse failed command session");
		push(GattEventKind::disconnected, 0, 6); b->client.reject = false; connect(); authenticate(true);
		check(invoke("button.observe", 8) == Status::ok, "button enabled in new generation of session");
		push(GattEventKind::subscribed, 40, 8); push(GattEventKind::write, 40, 8); push(GattEventKind::write, 40, 8);
		capture.remove = true; const auto outputs = capture.cbor_calls; push(GattEventKind::notification, 40, 0, press);
		check(!engine.device(id) && capture.buttons == 2, "decoded button Action can remove the device");
		check(capture.cbor_calls == outputs + 1,
		    "raw diagnostic delivered first; decoded CBOR suppressed after removal from typed Action");
		std::cout << "PASS owner KDF/AES vectors, mutual auth, both callback orders, button replay and write failure\n";
		return 0;
	}catch(const std::exception &e){std::cerr << e.what() << '\n'; return 1;}
}
