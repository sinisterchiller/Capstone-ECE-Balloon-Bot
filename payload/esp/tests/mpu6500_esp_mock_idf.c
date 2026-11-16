/*
 * SPDX-FileCopyrightText: 2024 ECE Balloon Bot Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Mock ESP-IDF behind drivers/mpu6500/mpu6500_esp.c for the host test
 * tests/mpu6500_esp_adapter_host_test.c. It implements the calls the adapter
 * makes - the I2C master API, logging, esp_timer and FreeRTOS delay - against
 * the MPU-6500 register model in tests/mpu6500_mock_device.h, so the unmodified
 * adapter and the unmodified vendor driver run on the host.
 *
 * Only one device is modelled, matching the adapter's own single-device
 * contract, and every transfer is decoded the way the adapter builds it:
 * a register write is one buffer of register + payload, a register read is a
 * one-byte register address followed by the read length.
 */

#include <stdarg.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "mpu6500_esp_mock_idf.h"
#include "mpu6500_mock_device.h"

struct mock_i2c_bus_t {
    i2c_port_t port;
};

struct mock_i2c_dev_t {
    i2c_master_bus_handle_t bus;
    uint16_t address; /* 7-bit */
    uint32_t scl_speed_hz;
};

static struct mock_i2c_bus_t s_bus;
static struct mock_i2c_dev_t s_dev;

static struct {
    mpu6500_mock_device_t chip;
    mpu6500_mock_i2c_stats_t stats;
    esp_err_t bus_create_result; /* ESP_OK disables the fault */
    int fail_read_register;      /* -1 disables the fault */
    int64_t time_us;
} s_mock;

void mpu6500_mock_i2c_reset(void)
{
    mpu6500_mock_device_power_on(&s_mock.chip);
    s_mock.stats = (mpu6500_mock_i2c_stats_t){ 0 };
    s_mock.bus_create_result = ESP_OK;
    s_mock.fail_read_register = -1;
    s_mock.time_us = 0;
}

mpu6500_mock_device_t *mpu6500_mock_i2c_chip(void)
{
    return &s_mock.chip;
}

void mpu6500_mock_i2c_fail_bus_create(esp_err_t err)
{
    s_mock.bus_create_result = err;
}

void mpu6500_mock_i2c_fail_reads_of_register(int reg)
{
    s_mock.fail_read_register = reg;
}

void mpu6500_mock_i2c_set_time_us(int64_t time_us)
{
    s_mock.time_us = time_us;
}

void mpu6500_mock_i2c_get_stats(mpu6500_mock_i2c_stats_t *out_stats)
{
    *out_stats = s_mock.stats;
}

esp_err_t i2c_new_master_bus(const i2c_master_bus_config_t *bus_config,
                             i2c_master_bus_handle_t *ret_bus)
{
    if ((bus_config == NULL) || (ret_bus == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_mock.bus_create_result != ESP_OK) {
        *ret_bus = NULL;
        return s_mock.bus_create_result;
    }

    s_bus.port = bus_config->i2c_port;
    s_mock.stats.bus_creates++;
    s_mock.stats.bus_present = true;
    *ret_bus = &s_bus;

    return ESP_OK;
}

esp_err_t i2c_del_master_bus(i2c_master_bus_handle_t bus_handle)
{
    if (bus_handle != &s_bus) {
        return ESP_ERR_INVALID_ARG;
    }

    s_mock.stats.bus_deletes++;
    s_mock.stats.bus_present = false;

    return ESP_OK;
}

esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t bus_handle,
                                    const i2c_device_config_t *dev_config,
                                    i2c_master_dev_handle_t *ret_handle)
{
    if ((bus_handle != &s_bus) || (dev_config == NULL) || (ret_handle == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }

    s_dev.bus = bus_handle;
    s_dev.address = dev_config->device_address;
    s_dev.scl_speed_hz = dev_config->scl_speed_hz;
    s_mock.stats.device_adds++;
    s_mock.stats.device_present = true;
    *ret_handle = &s_dev;

    return ESP_OK;
}

esp_err_t i2c_master_bus_rm_device(i2c_master_dev_handle_t handle)
{
    if (handle != &s_dev) {
        return ESP_ERR_INVALID_ARG;
    }

    s_mock.stats.device_removes++;
    s_mock.stats.device_present = false;

    return ESP_OK;
}

/* Reads fail as a whole when the injected register falls inside the range the
 * transfer covers. */
static bool read_fails(uint8_t reg, size_t read_size)
{
    return (s_mock.fail_read_register >= (int)reg) &&
           (s_mock.fail_read_register < (int)(reg + read_size));
}

esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t i2c_dev, const uint8_t *write_buffer,
                                      size_t write_size, uint8_t *read_buffer, size_t read_size,
                                      int xfer_timeout_ms)
{
    (void)xfer_timeout_ms;

    if ((i2c_dev != &s_dev) || (write_buffer == NULL) || (write_size == 0) ||
        (read_buffer == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (read_fails(write_buffer[0], read_size)) {
        return ESP_FAIL; /* the transfer did not complete */
    }

    mpu6500_mock_device_burst_read(&s_mock.chip, write_buffer[0], read_buffer, (uint16_t)read_size);

    return ESP_OK;
}

esp_err_t i2c_master_transmit(i2c_master_dev_handle_t i2c_dev, const uint8_t *write_buffer,
                              size_t write_size, int xfer_timeout_ms)
{
    (void)xfer_timeout_ms;

    if ((i2c_dev != &s_dev) || (write_buffer == NULL) || (write_size == 0)) {
        return ESP_ERR_INVALID_ARG;
    }

    mpu6500_mock_device_burst_write(&s_mock.chip, write_buffer[0], &write_buffer[1],
                                    (uint16_t)(write_size - 1));

    return ESP_OK;
}

int64_t esp_timer_get_time(void)
{
    return s_mock.time_us;
}

/* Counted, never slept: the driver's init polling loops run instantly. */
void vTaskDelay(TickType_t ticks)
{
    (void)ticks;
    s_mock.stats.delay_calls++;
}

const char *esp_err_to_name(esp_err_t code)
{
    switch (code) {
        case ESP_OK:
            return "ESP_OK";
        case ESP_FAIL:
            return "ESP_FAIL";
        case ESP_ERR_NO_MEM:
            return "ESP_ERR_NO_MEM";
        case ESP_ERR_INVALID_ARG:
            return "ESP_ERR_INVALID_ARG";
        case ESP_ERR_INVALID_STATE:
            return "ESP_ERR_INVALID_STATE";
        case ESP_ERR_INVALID_SIZE:
            return "ESP_ERR_INVALID_SIZE";
        case ESP_ERR_NOT_FOUND:
            return "ESP_ERR_NOT_FOUND";
        case ESP_ERR_NOT_SUPPORTED:
            return "ESP_ERR_NOT_SUPPORTED";
        case ESP_ERR_INVALID_RESPONSE:
            return "ESP_ERR_INVALID_RESPONSE";
        default:
            return "ESP_ERR_UNKNOWN";
    }
}

/* The adapter's log lines are not assertions: accepting them keeps the
 * ESP_LOGE/ESP_LOGW/ESP_LOGD/ESP_LOGI call sites in mpu6500_esp.c linked. */
void mpu6500_mock_esp_log(mpu6500_mock_log_level_t level, const char *tag, const char *format, ...)
{
    (void)level;
    (void)tag;
    (void)format;
}
