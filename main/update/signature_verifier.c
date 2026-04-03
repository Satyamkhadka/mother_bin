/**
 * @file signature_verifier.c
 * @brief Ed25519 firmware signature verification
 * 
 * Uses mbedtls for Ed25519 signature verification.
 * 
 * NOTE: Ed25519 support in mbedtls requires specific configuration.
 * In sdkconfig.defaults, ensure:
 *   CONFIG_MBEDTLS_ED25519_C=y
 * 
 * The Ed25519 public key (32 bytes) should be embedded in the firmware.
 * During manufacturing or first boot, this key can be programmed.
 */

#include "signature_verifier.h"
#include "dlm_config.h"
#include "storage/nvs_manager.h"
#include "esp_log.h"
#include "mbedtls/md.h"
#include <string.h>

/* Ed25519 verification stub - ESP-IDF mbedtls doesn't expose ed25519.h publicly.
 * Replace this with your own implementation or use libsodium component.
 * Returns 0 on success, non-zero on failure. */
static int mbedtls_ed25519_verify(const uint8_t *message, size_t message_len,
                                   const uint8_t *ctx, size_t ctx_len,
                                   const uint8_t public_key[32],
                                   const uint8_t signature[64])
{
    ESP_LOGW("ed25519", "Ed25519 verification stub called - implement or use libsodium");
    return -1;  /* Always fail - needs real implementation */
}

static const char *TAG = "sig_verify";

/* ============== Embedded Public Key ==============
 * 
 * THIS IS A PLACEHOLDER PUBLIC KEY!
 * Replace this with your actual Ed25519 public key (32 bytes in hex).
 * 
 * To generate a keypair:
 *   openssl genpkey -algorithm Ed25519 -out private.pem
 *   openssl pkey -in private.pem -pubout -out public.pem
 * 
 * To extract raw public key bytes:
 *   openssl pkey -in public.pem -pubin -outform DER | tail -c 32 > pubkey.bin
 *   xxd -i pubkey.bin
 * 
 * Or use libsodium/libs签:
 *   #include <sodium.h>
 *   unsigned char pk[crypto_sign_PUBLICKEYBYTES];
 *   unsigned char sk[crypto_sign_SECRETKEYBYTES];
 *   crypto_sign_keypair(pk, sk);
 */

/* PLACEHOLDER: All zeros - verification will fail until you add real key */
static uint8_t s_embedded_public_key[ED25519_PUBLIC_KEY_SIZE] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static uint8_t s_public_key[ED25519_PUBLIC_KEY_SIZE];
static bool s_key_loaded = false;

/* ============== Ed25519 Verification ==============
 * 
 * mbedTLS 2.x/3.x has Ed25519 support but the API may vary.
 * The following uses the standard Ed25519 verify interface.
 * 
 * If your mbedtls doesn't have Ed25519, you can:
 * 1. Use libsodium component (espressif/libsodium)
 * 2. Use a standalone Ed25519 implementation
 * 3. Use ECDSA-P256 instead (change signature_verifier.h)
 */

/**
 * @brief Verify Ed25519 signature
 * 
 * @param message       Message that was signed
 * @param message_len   Message length
 * @param signature     Signature (64 bytes)
 * @param public_key    Public key (32 bytes)
 * @return 0 on success, non-zero on failure
 */
static int ed25519_verify(const uint8_t *message, size_t message_len,
                          const uint8_t signature[ED25519_SIGNATURE_SIZE],
                          const uint8_t public_key[ED25519_PUBLIC_KEY_SIZE])
{
    /* mbedtls_ed25519_verify returns 0 on success */
    return mbedtls_ed25519_verify(message, message_len, 
                                   NULL, 0,  /* no context */
                                   public_key, signature);
}

/* ============== Public API ============== */

esp_err_t signature_verifier_init(void)
{
    if (s_key_loaded) {
        return ESP_OK;
    }
    
    /* Try to load key from NVS (allows runtime key rotation) */
    size_t key_len = sizeof(s_public_key);
    esp_err_t err = nvs_manager_get_blob("ota_config", "sign_pubkey",
                                          s_public_key, &key_len);
    
    if (err == ESP_OK && key_len == ED25519_PUBLIC_KEY_SIZE) {
        ESP_LOGI(TAG, "Loaded public key from NVS");
        s_key_loaded = true;
        return ESP_OK;
    }
    
    /* Use embedded key */
    memcpy(s_public_key, s_embedded_public_key, ED25519_PUBLIC_KEY_SIZE);
    
    /* Check if embedded key is valid (not all zeros) */
    bool all_zeros = true;
    for (int i = 0; i < ED25519_PUBLIC_KEY_SIZE; i++) {
        if (s_public_key[i] != 0) {
            all_zeros = false;
            break;
        }
    }
    
    if (all_zeros) {
        ESP_LOGW(TAG, "WARNING: Using placeholder public key (all zeros)");
        ESP_LOGW(TAG, "Signature verification will FAIL until you set a real key!");
        ESP_LOGW(TAG, "Set key using: idf.py menuconfig -> DLM Config");
        ESP_LOGW(TAG, "Or call signature_verifier_set_key() at runtime");
    } else {
        ESP_LOGI(TAG, "Using embedded public key");
        s_key_loaded = true;
    }
    
    return ESP_OK;
}

esp_err_t signature_verifier_set_key(const uint8_t public_key[ED25519_PUBLIC_KEY_SIZE])
{
    if (public_key == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    memcpy(s_public_key, public_key, ED25519_PUBLIC_KEY_SIZE);
    s_key_loaded = true;
    
    /* Save to NVS for persistence */
    esp_err_t err = nvs_manager_set_blob("ota_config", "sign_pubkey",
                                          public_key, ED25519_PUBLIC_KEY_SIZE);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to save key to NVS: %s", esp_err_to_name(err));
    }
    
    ESP_LOGI(TAG, "Public key updated");
    return ESP_OK;
}

esp_err_t signature_verifier_check(const uint8_t *firmware_data,
                                    size_t firmware_len,
                                    const uint8_t *signature,
                                    size_t signature_len)
{
    if (!s_key_loaded) {
        signature_verifier_init();
    }
    
    if (firmware_data == NULL || signature == NULL) {
        ESP_LOGE(TAG, "Invalid arguments");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (signature_len != ED25519_SIGNATURE_SIZE) {
        ESP_LOGE(TAG, "Invalid signature length: %d (expected %d)",
                 (int)signature_len, ED25519_SIGNATURE_SIZE);
        return ESP_ERR_INVALID_SIZE;
    }
    
    ESP_LOGI(TAG, "Verifying firmware signature (%d bytes)...", (int)firmware_len);
    
    /* Verify Ed25519 signature */
    int ret = ed25519_verify(firmware_data, firmware_len, signature, s_public_key);
    
    if (ret != 0) {
        ESP_LOGE(TAG, "Signature verification FAILED: %d", ret);
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Signature verified successfully!");
    return ESP_OK;
}

esp_err_t signature_verifier_hash(const uint8_t *data, size_t len, 
                                   uint8_t hash_out[32])
{
    if (data == NULL || hash_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    const mbedtls_md_info_t *md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (md_info == NULL) {
        return ESP_FAIL;
    }
    
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    
    int ret = mbedtls_md_setup(&ctx, md_info, 0);
    if (ret != 0) {
        mbedtls_md_free(&ctx);
        return ESP_FAIL;
    }
    
    ret = mbedtls_md_starts(&ctx);
    if (ret != 0) {
        mbedtls_md_free(&ctx);
        return ESP_FAIL;
    }
    
    ret = mbedtls_md_update(&ctx, data, len);
    if (ret != 0) {
        mbedtls_md_free(&ctx);
        return ESP_FAIL;
    }
    
    ret = mbedtls_md_finish(&ctx, hash_out);
    mbedtls_md_free(&ctx);
    
    return (ret == 0) ? ESP_OK : ESP_FAIL;
}
