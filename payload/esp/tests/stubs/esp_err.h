/*
 * SPDX-FileCopyrightText: 2024 ECE Balloon Bot Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Host stand-in for the ESP-IDF <esp_err.h>, used by the MPU host tests so that
 * main/mpu.h, main/mpu.cpp and drivers/mpu6500/mpu6500_esp.c can be compiled and
 * linked without ESP-IDF.
 *
 * The numeric values are the ESP-IDF ones (esp_err.h: ESP_ERR_NO_MEM 0x101 ...
 * ESP_ERR_NOT_SUPPORTED 0x106, ESP_ERR_INVALID_RESPONSE 0x108) so the tests
 * assert the same codes the firmware returns. This header is on the include path
 * of the host test scripts only - never of the ESP-IDF build; esp_err_to_name()
 * is implemented by tests/mpu6500_esp_mock_idf.c.
 */

#pragma once

typedef int esp_err_t;

#define ESP_OK                    0
#define ESP_FAIL                  -1

#define ESP_ERR_NO_MEM            0x101
#define ESP_ERR_INVALID_ARG       0x102
#define ESP_ERR_INVALID_STATE     0x103
#define ESP_ERR_INVALID_SIZE      0x104
#define ESP_ERR_NOT_FOUND         0x105
#define ESP_ERR_NOT_SUPPORTED     0x106
#define ESP_ERR_INVALID_RESPONSE  0x108

const char *esp_err_to_name(esp_err_t code);
