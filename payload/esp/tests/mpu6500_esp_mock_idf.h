/*
 * SPDX-FileCopyrightText: 2024 ECE Balloon Bot Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Control surface for the mock ESP-IDF that tests/run_mpu6500_host_test.sh links
 * under drivers/mpu6500/mpu6500_esp.c. The mock itself lives in
 * tests/mpu6500_esp_mock_idf.c; this header is what the adapter test uses to set
 * up the fake sensor, inject I2C faults and inspect what reached the bus.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "mpu6500_mock_device.h"

typedef struct {
    unsigned bus_creates;    /* i2c_new_master_bus() successes */
    unsigned bus_deletes;    /* i2c_del_master_bus() calls */
    unsigned device_adds;    /* i2c_master_bus_add_device() successes */
    unsigned device_removes; /* i2c_master_bus_rm_device() calls */
    unsigned delay_calls;    /* vTaskDelay() calls */
    bool bus_present;
    bool device_present;
} mpu6500_mock_i2c_stats_t;

/* Power-on state: an MPU-6500 with WHO_AM_I 0x70, no bus or device, no faults. */
void mpu6500_mock_i2c_reset(void);

/* The fake sensor's register file, so a test can seed sample data and inspect
 * the configuration the adapter wrote. */
mpu6500_mock_device_t *mpu6500_mock_i2c_chip(void);

/* Let i2c_new_master_bus() fail with this code; ESP_OK (0) disables the fault. */
void mpu6500_mock_i2c_fail_bus_create(esp_err_t err);

/* Make every read transfer that covers this register fail, as a sensor that does
 * not answer a register read does; -1 disables the fault. Writes are unaffected. */
void mpu6500_mock_i2c_fail_reads_of_register(int reg);

/* esp_timer_get_time() return value, i.e. the timestamp a read is stamped with. */
void mpu6500_mock_i2c_set_time_us(int64_t time_us);

void mpu6500_mock_i2c_get_stats(mpu6500_mock_i2c_stats_t *out_stats);
