/**
 * @file signature_verifier.h
 * @brief Ed25519 firmware signature verification
 * 
 * Uses mbedtls for Ed25519 signature verification.
 * The public key should be embedded in the firmware.
 */

#pragma once

#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Ed25519 public key size (32 bytes)
 */
#define ED25519_PUBLIC_KEY_SIZE     32

/**
 * @brief Ed25519 signature size (64 bytes)
 */
#define ED25519_SIGNATURE_SIZE      64

/**
 * @brief Initialize signature verifier
 * 
 * Loads the embedded public key.
 * 
 * @return ESP_OK on success
 */
esp_err_t signature_verifier_init(void);

/**
 * @brief Verify Ed25519 signature of firmware
 * 
 * @param firmware_data     Firmware binary data
 * @param firmware_len      Length of firmware
 * @param signature         Ed25519 signature (64 bytes)
 * @param signature_len     Length of signature
 * @return ESP_OK if signature valid, ESP_FAIL if invalid, error code on other errors
 */
esp_err_t signature_verifier_check(const uint8_t *firmware_data,
                                    size_t firmware_len,
                                    const uint8_t *signature,
                                    size_t signature_len);

/**
 * @brief Set public key at runtime (optional)
 * 
 * Normally the public key is embedded at compile time.
 * This allows runtime key rotation for advanced use cases.
 * 
 * @param public_key    32-byte Ed25519 public key
 * @return ESP_OK on success
 */
esp_err_t signature_verifier_set_key(const uint8_t public_key[ED25519_PUBLIC_KEY_SIZE]);

/**
 * @brief Calculate SHA-256 hash of firmware
 * 
 * Helper function for debugging or additional verification.
 * 
 * @param data      Data to hash
 * @param len       Data length
 * @param hash_out  Output buffer (32 bytes)
 * @return ESP_OK on success
 */
esp_err_t signature_verifier_hash(const uint8_t *data, size_t len, uint8_t hash_out[32]);

#ifdef __cplusplus
}
#endif
