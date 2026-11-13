/*
 * SPDX-FileCopyrightText: 2024 ECE Balloon Bot Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Host stand-in for the ESP-IDF <esp_check.h>: only ESP_RETURN_ON_FALSE, the one
 * macro drivers/mpu6500/mpu6500_esp.c uses, is reproduced with the same
 * log-then-return semantics.
 */

#pragma once

#include "esp_err.h"
#include "esp_log.h"

#define ESP_RETURN_ON_FALSE(a, err_code, tag, format, ...)                  \
    do {                                                                   \
        if (!(a)) {                                                        \
            ESP_LOGE(tag, format, ##__VA_ARGS__);                          \
            return err_code;                                               \
        }                                                                  \
    } while (0)
