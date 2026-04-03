/**
 * @file custom_provider.h
 * @brief Custom Server OTA Provider (PLACEHOLDER)
 * 
 * Implement this provider to use your own OTA server.
 * See custom_provider.c for implementation guide.
 */

#pragma once

#include "update/ota_provider.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Get the custom server provider instance
 * 
 * NOTE: This returns a placeholder implementation.
 * You must implement custom_provider.c for your server.
 */
const ota_provider_t* ota_provider_get_custom(void);

#ifdef __cplusplus
}
#endif
