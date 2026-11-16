/*
 * SPDX-FileCopyrightText: 2024 ECE Balloon Bot Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Host stand-in for the ESP-IDF <driver/i2c_master.h>. It declares exactly the
 * types and calls drivers/mpu6500/mpu6500_esp.c uses; the implementation in
 * tests/mpu6500_esp_mock_idf.c decodes them against the MPU-6500 register model
 * in tests/mpu6500_mock_device.h instead of talking to a real bus.
 *
 * The handle types are pointers to host-mock structs and stay opaque here.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef enum {
    I2C_NUM_0 = 0,
    I2C_NUM_1,
} i2c_port_t;

typedef enum {
    I2C_CLK_SRC_DEFAULT = 0,
} i2c_clock_source_t;

typedef enum {
    I2C_ADDR_BIT_LEN_7 = 0,
    I2C_ADDR_BIT_LEN_10,
} i2c_addr_bit_len_t;

typedef struct mock_i2c_bus_t *i2c_master_bus_handle_t;
typedef struct mock_i2c_dev_t *i2c_master_dev_handle_t;

typedef struct {
    i2c_port_t i2c_port;
    int sda_io_num;
    int scl_io_num;
    i2c_clock_source_t clk_source;
    uint8_t glitch_ignore_cnt;
    struct {
        uint32_t enable_internal_pullup : 1;
    } flags;
} i2c_master_bus_config_t;

typedef struct {
    i2c_addr_bit_len_t dev_addr_length;
    uint16_t device_address;
    uint32_t scl_speed_hz;
} i2c_device_config_t;

esp_err_t i2c_new_master_bus(const i2c_master_bus_config_t *bus_config,
                             i2c_master_bus_handle_t *ret_bus);
esp_err_t i2c_del_master_bus(i2c_master_bus_handle_t bus_handle);
esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t bus_handle,
                                    const i2c_device_config_t *dev_config,
                                    i2c_master_dev_handle_t *ret_handle);
esp_err_t i2c_master_bus_rm_device(i2c_master_dev_handle_t handle);
esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t i2c_dev, const uint8_t *write_buffer,
                                      size_t write_size, uint8_t *read_buffer, size_t read_size,
                                      int xfer_timeout_ms);
esp_err_t i2c_master_transmit(i2c_master_dev_handle_t i2c_dev, const uint8_t *write_buffer,
                              size_t write_size, int xfer_timeout_ms);
