/*
 * SPDX-FileCopyrightText: 2024 ECE Balloon Bot Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ESP-IDF I2C adapter for the LibDriver MPU-6500 driver (six-axis).
 *
 * Vendor source: libdriver/mpu6500 v1.0.12,
 * commit 9d1b7f425b05edd9621f710e40cb84d381216577 (see README.md).
 * Vendor files (src/driver_mpu6500.c, src/driver_mpu6500.h,
 * src/driver_mpu6500_code.h, LICENSE) are unmodified.
 *
 * All register access and scaling remain inside the LibDriver driver; this
 * component only supplies the ESP-IDF I2C transport, the sensor configuration and
 * the translation to ESP-IDF types/error codes.
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "esp_err.h"

/**
 * @brief One six-axis MPU-6500 sample
 *
 * Both triplets are populated on ESP_OK. The values are scaled by the LibDriver
 * driver from the configured full-scale ranges (+-2 g and +-2000 dps).
 */
typedef struct {
    float accel_g[3];      /*!< Accelerometer x/y/z in g */
    float gyro_dps[3];     /*!< Gyroscope x/y/z in degrees per second */
    int64_t timestamp_us;  /*!< esp_timer timestamp (us since boot) when data was read */
} mpu6500_esp_reading_t;

/**
 * @brief MPU-6500 device handle (opaque)
 */
typedef struct mpu6500_esp_dev_t *mpu6500_esp_handle_t;

/**
 * @brief Initialize the MPU-6500 over I2C
 *
 * Creates I2C master bus 0 with SDA=GPIO21, SCL=GPIO22 at 100 kHz and adds the
 * MPU-6500 (0x68, AD0 tied to GND) to it. Then runs the LibDriver init sequence:
 * WHO_AM_I check, device reset, 50 Hz sample rate, PLL clock source, DLPF 3,
 * +-2 g and +-2000 dps.
 *
 * The bus is owned by this component until mpu6500_esp_deinit().
 *
 * The LibDriver callbacks carry no context pointer, so this component supports
 * one initialized device at a time.
 *
 * @param out_handle Output handle; set to NULL on failure
 *
 * @return
 *  - ESP_OK: MPU-6500 initialized
 *  - ESP_ERR_INVALID_ARG: out_handle is NULL
 *  - ESP_ERR_NO_MEM: the device context or an I2C driver resource could not be
 *    allocated
 *  - ESP_ERR_INVALID_STATE: a device is already initialized, I2C bus 0 is
 *    already in use, or the LibDriver rejected the linked callbacks
 *  - ESP_ERR_NOT_FOUND: no MPU-6500 answered on the bus (WHO_AM_I 0x75 != 0x70)
 *  - ESP_ERR_INVALID_RESPONSE: the diagnostic WHO_AM_I preflight read ran
 *    before the LibDriver init and the I2C transfer itself failed, so the
 *    transfer did not complete
 *  - ESP_FAIL: I2C bus/device setup, device reset or register configuration failed
 *
 * On failure the I2C bus, its device and the context are released.
 */
esp_err_t mpu6500_esp_init(mpu6500_esp_handle_t *out_handle);

/**
 * @brief Read one six-axis sample
 *
 * Reads the accelerometer and gyroscope output registers through the LibDriver
 * polling API (no FIFO, no interrupt) and stamps the result with
 * esp_timer_get_time().
 *
 * @param handle Handle from mpu6500_esp_init()
 * @param out_reading Output sample (zeroed before acquisition)
 *
 * @return
 *  - ESP_OK: both triplets and the timestamp are valid
 *  - ESP_ERR_INVALID_ARG: NULL argument
 *  - ESP_ERR_INVALID_STATE: handle not initialized, or the device is in a mode
 *    the polling read does not support (DMP/FIFO)
 *  - ESP_ERR_INVALID_RESPONSE: an I2C register read failed
 *  - ESP_ERR_INVALID_SIZE: the requested sample length was rejected
 *  - ESP_FAIL: other LibDriver error
 */
esp_err_t mpu6500_esp_read(mpu6500_esp_handle_t handle, mpu6500_esp_reading_t *out_reading);

/**
 * @brief Power down the MPU-6500, then release the I2C bus
 *
 * The MPU-6500 is put into sleep mode with the clock stopped and the I2C bus,
 * the device handle and the context are freed.
 *
 * @param handle Handle from mpu6500_esp_init(); the context is freed, so the
 *               handle must not be used again
 * @return
 *  - ESP_OK: sensor powered down and resources released (a failed power-down
 *    register write is logged as a warning)
 *  - ESP_ERR_INVALID_ARG: handle is NULL
 *  - ESP_ERR_INVALID_STATE: handle is not in the initialized state
 */
esp_err_t mpu6500_esp_deinit(mpu6500_esp_handle_t handle);

#ifdef __cplusplus
}
#endif
