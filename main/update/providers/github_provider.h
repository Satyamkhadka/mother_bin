/**
 * @file github_provider.h
 * @brief GitHub releases OTA provider
 */

#pragma once

#include "update/ota_provider.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Get the GitHub releases provider instance
 */
const ota_provider_t* ota_provider_get_github(void);

#ifdef __cplusplus
}
#endif
