/*
 * SPDX-FileCopyrightText: 2024 ECE Balloon Bot Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Host stand-in for the ESP-IDF <freertos/task.h>: vTaskDelay() is counted by
 * tests/mpu6500_esp_mock_idf.c instead of slept.
 */

#pragma once

#include "freertos/FreeRTOS.h"

void vTaskDelay(TickType_t ticks);
