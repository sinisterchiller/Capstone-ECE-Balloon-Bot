/*
 * SPDX-FileCopyrightText: 2024 ECE Balloon Bot Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Host stand-in for the ESP-IDF <freertos/FreeRTOS.h>. Only the two symbols
 * drivers/mpu6500/mpu6500_esp.c uses for its delay hook are reproduced: the
 * tick type and the millisecond conversion. The host never sleeps, so a tick is
 * a millisecond here. This header is on the include path of
 * tests/run_mpu6500_host_test.sh only - never of the ESP-IDF build.
 */

#pragma once

#include <stdint.h>

typedef uint32_t TickType_t;

#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
