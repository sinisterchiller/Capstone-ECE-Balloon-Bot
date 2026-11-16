/*
 * SPDX-FileCopyrightText: 2024 ECE Balloon Bot Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Host stand-in for the ESP-IDF <esp_timer.h>. The timestamp returned is set by
 * the test through tests/mpu6500_esp_mock_idf.h, so mpu6500_esp_read()'s stamped
 * timestamp can be asserted exactly.
 */

#pragma once

#include <stdint.h>

int64_t esp_timer_get_time(void);
