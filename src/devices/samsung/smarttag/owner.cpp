// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Oleg Kozhevnikov
#include "NaBTools/devices/samsung/smarttag/owner.hpp"
#include <algorithm>
#include <limits>

namespace NaBTools::runtime
{
	namespace
	{
		constexpr std::array<uint8_t, 16> text{'s', 'm', 'a', 'r', 't', 't', 'h', 'i', 'n', 'g', 's', 5, 5, 5, 5, 5};
// EI-T5300 also returns the NUL-terminated string (12 bytes + PKCS7=4),
// verified on hardware. Accept these two exact proofs, never a prefix match.
		constexpr std::array<uint8_t, 16> text_nul{'s', 'm', 'a', 'r', 't', 't', 'h', 'i', 'n', 'g', 's', 0, 4, 4, 4,
			                                       4};
		constexpr std::array<uint8_t, 17> label{'b', 'l', 'e', 'A', 'u', 't', 'h', 'e', 'n', 't', 'i', 'c', 'a', 't',
			                                    'i', 'o', 'n'};
	}
	void smarttag_erase(std::span<uint8_t> bytes)
	{
		volatile uint8_t *p = bytes.data(); for(size_t i = 0; i < bytes.size(); ++i)
			p[i] = 0;
	}

	void SmartTagOwnerSession::reset()
	{
		smarttag_erase(master_); smarttag_erase(auth_key_); smarttag_erase(session_key_);
		smarttag_erase(owner_nonce_); smarttag_erase(tag_nonce_); phase_ = Phase::empty; tx_ = rx_ = 0;
		rx_seen_ = false; crypto_ = {};
	}

	bool SmartTagOwnerSession::derive(Bytes key, Bytes value, std::span<uint8_t, 16> output)
	{
		if(key.size() != 16 || value.size() > 17) return false;

		std::array<uint8_t, 37> input{}; std::array<uint8_t, 32> hash{};
		std::copy(key.begin(), key.end(), input.begin()); input[19] = 1;
		std::copy(value.begin(), value.end(), input.begin() + 20);
		const bool ok = crypto_.sha256(crypto_.context, Bytes(input.data(), 20 + value.size()), hash);
		if(ok) std::copy_n(hash.begin(), 16, output.begin());

		smarttag_erase(input); smarttag_erase(hash); return ok;
	}

	bool SmartTagOwnerSession::begin(SmartTagCrypto crypto, Bytes master)
	{
		reset(); if(!crypto || master.size() != 16)
			return false;

		crypto_ = crypto;
		std::copy(master.begin(), master.end(), master_.begin());
		if(!derive(master_, label, auth_key_) || !crypto_.random(crypto_.context, owner_nonce_)){reset(); return false;}

		phase_ = Phase::nonce; return true;
	}

	bool SmartTagOwnerSession::challenge(Bytes nonce, std::span<uint8_t, 16> proof)
	{
		if(phase_ != Phase::nonce || nonce.size() != 16){reset(); return false;}

		std::copy(nonce.begin(), nonce.end(), tag_nonce_.begin());
		if(!derive(master_, tag_nonce_, session_key_) || !crypto_.cbc128(crypto_.context, true, auth_key_, tag_nonce_,
		    text, proof)){reset(); return false;}

		smarttag_erase(master_); phase_ = Phase::proof; return true;
	}

	bool SmartTagOwnerSession::verify(Bytes proof)
	{
		if(phase_ != Phase::proof || proof.size() != 16){reset(); return false;}

		std::array<uint8_t, 16> expected{}, expected_nul{};
		bool ok = crypto_.cbc128(crypto_.context, true, auth_key_, owner_nonce_, text, expected);
		const bool ok_nul = crypto_.cbc128(crypto_.context, true, auth_key_, owner_nonce_, text_nul, expected_nul);
		uint8_t difference = 0, difference_nul = 0;
		for(size_t i = 0; i < 16; ++i)
		{
			difference |= expected[i] ^ proof[i]; difference_nul |= expected_nul[i] ^ proof[i];
		}
		smarttag_erase(expected); smarttag_erase(expected_nul); smarttag_erase(auth_key_); smarttag_erase(owner_nonce_);
		if(!ok || !ok_nul || (difference && difference_nul))
		{
			reset(); return false;
		}

		phase_ = Phase::authenticated; return true;
	}

	bool SmartTagOwnerSession::command(uint8_t opcode, Bytes arguments, std::span<uint8_t, 16> encrypted)
	{
		if(!authenticated() || arguments.size() > 10 || tx_ == std::numeric_limits<uint32_t>::max()) return false;

		std::array<uint8_t, 16> plain; const auto count = ++tx_;
		for(unsigned i = 0; i < 4; ++i)
			plain[i] = static_cast<uint8_t>(count >> (8 * i));
		plain[4] = opcode;
		std::copy(arguments.begin(), arguments.end(), plain.begin() + 5);
		std::fill(plain.begin() + 5 + arguments.size(), plain.end(), uint8_t(11 - arguments.size()));
		const bool ok = crypto_.cbc128(crypto_.context, true, session_key_, tag_nonce_, plain, encrypted);
		smarttag_erase(plain); if(!ok)
			reset();

		return ok;
	}

	bool SmartTagOwnerSession::button(Bytes encrypted, uint32_t &counter, uint8_t &action)
	{
		if(!authenticated() || encrypted.size() != 16) return false;

		std::array<uint8_t, 16> plain{};
		const bool ok = crypto_.cbc128(crypto_.context, false, session_key_, tag_nonce_, encrypted, plain);
		uint8_t bad = 0; for(size_t i = 5; i < 16; ++i)
			bad |= plain[i] ^ 11;
		uint32_t count = 0; for(unsigned i = 0; i < 4; ++i)
			count |= uint32_t(plain[i]) << (i * 8);
		const auto value = plain[4]; smarttag_erase(plain);
		if(!ok || bad || value < 1 || value > 3 || (rx_seen_ && count <= rx_)) return false;

		counter = rx_ = count; action = value; rx_seen_ = true; return true;
	}

}
