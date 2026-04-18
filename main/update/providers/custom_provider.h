/**
 * @file custom_provider.h
 * @brief Firmware Manager OTA Provider
 */

#pragma once

#include "update/ota_provider.h"

#ifdef __cplusplus
extern "C" {
#endif

const ota_provider_t* ota_provider_get_custom(void);

#ifdef __cplusplus
}
#endif
