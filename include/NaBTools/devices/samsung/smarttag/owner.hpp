// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Oleg Kozhevnikov
#pragma once
#include "NaBTools/bt/gap/advertisement.hpp"
#include "NaBTools/core/device_id.hpp"

namespace NaBTools::runtime
{
// Cryptographic primitives are injected; no platform SDK or account API in core.
// CBC operates on complete blocks, without automatic padding, with distinct IO.
	struct SmartTagCrypto {
		void *context{};
		bool (*random)(void *, std::span<uint8_t>)
		{
		};
		bool (*sha256)(void *, Bytes, std::span<uint8_t, 32>)
		{
		};
		bool (*cbc128)(void *, bool encrypt, Bytes key, Bytes iv, Bytes input, std::span<uint8_t> output)
		{
		};
		explicit operator bool()const {
			return random && sha256 && cbc128;
		}
	};
	void smarttag_erase(std::span<uint8_t>);
	struct SmartTagOwnerAccess {
		SmartTagCrypto crypto;
		void *context{};
		// Called only by explicit owner.authenticate, in engine context. Application
		// selects the credential for this manually admitted instance. Never log it.
		bool (*load)(void *, DeviceId, Address, std::span<uint8_t, 16> master_secret)
		{
		};
		explicit operator bool()const {
			return bool(crypto) && load;
		}
	};
// Independent implementation of the original SmartTag owner protocol:
// https://arxiv.org/pdf/2210.14702 sections 3.1, 3.2 (v2, July 2025).
// masterSecret = first 16 decoded bytes of bleD2D.encryptionKey, not an LTK
// and not a Samsung Find EC key used for location encryption.
	class SmartTagOwnerSession
	{
		SmartTagCrypto crypto_;
		std::array<uint8_t, 16> master_{}, auth_key_{}, session_key_{}, owner_nonce_{}, tag_nonce_{};
		enum class Phase {empty, nonce, proof, authenticated};
		Phase phase_{Phase::empty};
		uint32_t tx_{}, rx_{};
		bool rx_seen_{};
		bool derive(Bytes key, Bytes label, std::span<uint8_t, 16> output);
	public:
		~SmartTagOwnerSession()
		{
			reset();
		}

		SmartTagOwnerSession() = default;
		SmartTagOwnerSession(const SmartTagOwnerSession &) = delete;
		SmartTagOwnerSession& operator=(const SmartTagOwnerSession &) = delete;
		void reset();
		bool begin(SmartTagCrypto, Bytes master_secret);
		Bytes nonce()const
		{
			return owner_nonce_;
		}

		bool challenge(Bytes nonce, std::span<uint8_t, 16> proof);
		bool verify(Bytes encrypted_proof);
		bool authenticated()const
		{
			return phase_ == Phase::authenticated;
		}

		// A prepared command consumes its counter. If the transport rejects/fails
		// the write, discard this session instead of retrying ambiguous ciphertext.
		bool command(uint8_t opcode, Bytes arguments, std::span<uint8_t, 16> encrypted);
		bool button(Bytes encrypted, uint32_t &counter, uint8_t &action);
	};
}
