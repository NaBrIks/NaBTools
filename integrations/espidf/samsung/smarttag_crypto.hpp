// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Oleg Kozhevnikov
#pragma once
#include "NaBTools/devices/samsung/smarttag/owner.hpp"
#include "esp_idf_version.h"
#include "esp_random.h"
#if ESP_IDF_VERSION_MAJOR >= 6
// ESP-IDF 6 exposes cryptographic primitives through PSA Crypto.
// https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32/migration-guides/release-6.x/6.0/security.html
#include "psa/crypto.h"
#else
#include "mbedtls/aes.h"
#include "mbedtls/sha256.h"
#endif
#include <algorithm>

namespace NaBTools::runtime
{
	inline SmartTagCrypto espidf_smarttag_crypto()
	{
		return {nullptr,
		        [](void *, std::span<uint8_t> output){
					esp_fill_random(output.data(), output.size()); return true;
				},
		        [](void *, Bytes input, std::span<uint8_t, 32> output){
#if ESP_IDF_VERSION_MAJOR >= 6
					size_t written = 0;
					return psa_crypto_init() == PSA_SUCCESS &&
					       psa_hash_compute(PSA_ALG_SHA_256, input.data(), input.size(), output.data(), output.size(),
					           &written) == PSA_SUCCESS && written == output.size();
#else
					return mbedtls_sha256(input.data(), input.size(), output.data(), 0) == 0;
#endif
				},
		        [](void *, bool encrypt, Bytes key, Bytes iv, Bytes input, std::span<uint8_t> output){
					if(key.size() != 16 || iv.size() != 16 || input.size() != output.size() || input.size() % 16)
						return false;

#if ESP_IDF_VERSION_MAJOR >= 6
					if(psa_crypto_init() != PSA_SUCCESS) return false;

					constexpr auto algorithm = PSA_ALG_CBC_NO_PADDING;
					auto attributes = psa_key_attributes_init();
					psa_set_key_type(&attributes, PSA_KEY_TYPE_AES);
					psa_set_key_bits(&attributes, 128);
					psa_set_key_algorithm(&attributes, algorithm);
					psa_set_key_usage_flags(&attributes, encrypt ? PSA_KEY_USAGE_ENCRYPT : PSA_KEY_USAGE_DECRYPT);
					mbedtls_svc_key_id_t key_id{};
					auto status = psa_import_key(&attributes, key.data(), key.size(), &key_id);
					psa_reset_key_attributes(&attributes);
					if(status != PSA_SUCCESS) return false;

					auto operation = psa_cipher_operation_init();
					status = encrypt ? psa_cipher_encrypt_setup(&operation, key_id, algorithm) :
					    psa_cipher_decrypt_setup(&operation, key_id, algorithm);
					if(status == PSA_SUCCESS)
						status = psa_cipher_set_iv(&operation, iv.data(), iv.size());

					size_t written = 0;
					if(status == PSA_SUCCESS)
						status = psa_cipher_update(&operation, input.data(), input.size(), output.data(), output.size(),
						        &written);

					std::array<uint8_t, 16> tail{};
					size_t remaining = 0;
					if(status == PSA_SUCCESS)
						status = psa_cipher_finish(&operation, tail.data(), tail.size(), &remaining);

					const bool complete = status == PSA_SUCCESS && written <= output.size() &&
					    remaining == output.size() - written;
					if(complete && remaining)
						std::copy_n(tail.data(), remaining, output.data() + written);

					smarttag_erase(tail);
					psa_cipher_abort(&operation);
					const auto destroy_status = psa_destroy_key(key_id);
					return complete && destroy_status == PSA_SUCCESS;
#else
					std::array<uint8_t, 16> working_iv; std::copy(iv.begin(), iv.end(), working_iv.begin());
					mbedtls_aes_context ctx; mbedtls_aes_init(&ctx);
					auto rc = encrypt ? mbedtls_aes_setkey_enc(&ctx, key.data(), 128) : mbedtls_aes_setkey_dec(&ctx,
					        key.data(), 128);
					if(!rc)
						rc = mbedtls_aes_crypt_cbc(&ctx, encrypt ? MBEDTLS_AES_ENCRYPT : MBEDTLS_AES_DECRYPT,
						        input.size(), working_iv.data(), input.data(), output.data());

					mbedtls_aes_free(&ctx); smarttag_erase(working_iv); return rc == 0;
#endif
				}};
	}

}
