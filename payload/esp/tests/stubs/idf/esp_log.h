/*
 * SPDX-FileCopyrightText: 2024 ECE Balloon Bot Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Host stand-in for the ESP-IDF <esp_log.h>. The logging macros resolve to the
 * mock's sink (tests/mpu6500_esp_mock_idf.c), which swallows the messages: the
 * host tests assert on return codes and the mock chip state, not on log text.
 */

#pragma once

#include "esp_err.h"

typedef enum {
    MPU6500_MOCK_LOG_ERROR = 0,
    MPU6500_MOCK_LOG_WARN,
    MPU6500_MOCK_LOG_INFO,
    MPU6500_MOCK_LOG_DEBUG,
} mpu6500_mock_log_level_t;

/* The first variadic argument is the format string, so a plain message with no
 * conversions (`ESP_LOGE(TAG, "Null handle")`) works without ##VA_ARGS. */
void mpu6500_mock_esp_log(mpu6500_mock_log_level_t level, const char *tag, const char *format,
                          ...);

#define ESP_LOGE(tag, ...) mpu6500_mock_esp_log(MPU6500_MOCK_LOG_ERROR, tag, __VA_ARGS__)
#define ESP_LOGW(tag, ...) mpu6500_mock_esp_log(MPU6500_MOCK_LOG_WARN, tag, __VA_ARGS__)
#define ESP_LOGI(tag, ...) mpu6500_mock_esp_log(MPU6500_MOCK_LOG_INFO, tag, __VA_ARGS__)
#define ESP_LOGD(tag, ...) mpu6500_mock_esp_log(MPU6500_MOCK_LOG_DEBUG, tag, __VA_ARGS__)
