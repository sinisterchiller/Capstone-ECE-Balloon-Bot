/*
 * SPDX-FileCopyrightText: 2024 ECE Balloon Bot Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Host stand-in for the ESP-IDF <esp_err.h>, used only by
 * tests/mpu_wrapper_host_test.cpp so that main/mpu.h and main/mpu.cpp can be
 * compiled and linked without ESP-IDF.
 *
 * The numeric values are the ESP-IDF ones (esp_err.h: ESP_ERR_NO_MEM 0x101 ...
 * ESP_ERR_NOT_SUPPORTED 0x106, ESP_ERR_INVALID_RESPONSE 0x108) so the test
 * asserts the same codes the firmware returns. This header is on the include
 * path of the host test script only - never of the ESP-IDF build.
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
