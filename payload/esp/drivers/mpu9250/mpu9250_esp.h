/*
 * SPDX-FileCopyrightText: 2024 ECE Balloon Bot Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ESP-IDF I2C adapter for the LibDriver MPU-9250 driver (MPU-9250 + AK8963).
 *
 * Vendor source: libdriver/mpu9250 v1.0.11,
 * commit 06cc13e8d6f69e00e7a95f31e5794ce72d77e145 (see README.md).
 * Vendor files (src/driver_mpu9250.c, src/driver_mpu9250.h,
 * src/driver_mpu9250_code.h, LICENSE) are unmodified.
 *
 * All register access, scaling and AK8963 sensitivity adjustment remain inside
 * the LibDriver driver; this component only supplies the ESP-IDF I2C transport,
 * the sensor configuration and the translation to ESP-IDF types/error codes.
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "esp_err.h"

/**
 * @brief One nine-axis MPU-9250 sample
 *
 * All nine axes are populated on ESP_OK. Accelerometer and gyroscope values are
 * scaled by the LibDriver driver from the configured full-scale ranges (default
 * +-2 g and +-2000 dps). Magnetometer values come from the AK8963 and already
 * include the per-axis sensitivity adjustment read from its fuse ROM.
 */
typedef struct {
    float accel_g[3];      /*!< Accelerometer x/y/z in g */
    float gyro_dps[3];     /*!< Gyroscope x/y/z in degrees per second */
    float mag_ut[3];       /*!< Magnetometer x/y/z in microtesla (AK8963) */
    int64_t timestamp_us;  /*!< esp_timer timestamp (us since boot) when data was read */
} mpu9250_esp_reading_t;

/**
 * @brief MPU-9250 device handle (opaque)
 */
typedef struct mpu9250_esp_dev_t *mpu9250_esp_handle_t;

/**
 * @brief Initialize the MPU-9250 and its AK8963 magnetometer over I2C
 *
 * Creates I2C master bus 0 with SDA=GPIO21, SCL=GPIO22 at 100 kHz and adds the
 * MPU-9250 (0x68, AD0 tied to GND) and the AK8963 (0x0C, reached through the
 * MPU's I2C bypass) to it. Then runs the LibDriver init sequence: WHO_AM_I
 * check, device reset, 50 Hz sample rate, PLL clock source, DLPF 3, +-2 g,
 * +-2000 dps, AK8963 reset with fuse-ROM sensitivity read, and 100 Hz
 * 16-bit continuous magnetometer mode.
 *
 * The bus is owned by this component until mpu9250_esp_deinit().
 *
 * The LibDriver callbacks carry no context pointer, so this component supports
 * one initialized device at a time.
 *
 * @param out_handle Output handle; set to NULL on failure
 *
 * @return
 *  - ESP_OK: MPU-9250 and AK8963 initialized
 *  - ESP_ERR_INVALID_ARG: out_handle is NULL
 *  - ESP_ERR_NO_MEM: the device context or an I2C driver resource could not be
 *    allocated
 *  - ESP_ERR_INVALID_STATE: a device is already initialized, I2C bus 0 is
 *    already in use, or the LibDriver rejected the linked callbacks
 *  - ESP_ERR_NOT_FOUND: no MPU-9250 (WHO_AM_I 0x75 != 0x71) or no AK8963
 *    (WIA != 0x48) answered on the bus
 *  - ESP_FAIL: I2C bus/device setup, device reset or register configuration failed
 *  - ESP_ERR_NOT_SUPPORTED: the LibDriver reported an unsupported interface
 *
 * On failure the I2C bus, its devices and the context are released.
 */
esp_err_t mpu9250_esp_init(mpu9250_esp_handle_t *out_handle);

/**
 * @brief Read one nine-axis sample
 *
 * Reads the accelerometer, gyroscope and magnetometer output registers through
 * the LibDriver polling API (no FIFO, no interrupt) and stamps the result with
 * esp_timer_get_time().
 *
 * @param handle Handle from mpu9250_esp_init()
 * @param out_reading Output sample (zeroed before acquisition)
 *
 * @return
 *  - ESP_OK: all nine axes and the timestamp are valid
 *  - ESP_ERR_INVALID_ARG: NULL argument
 *  - ESP_ERR_INVALID_STATE: handle not initialized, or the device is in a mode
 *    the polling read does not support (DMP/FIFO)
 *  - ESP_ERR_INVALID_RESPONSE: an I2C register read failed
 *  - ESP_ERR_INVALID_SIZE: the requested sample length was rejected
 *  - ESP_FAIL: other LibDriver error
 */
esp_err_t mpu9250_esp_read(mpu9250_esp_handle_t handle, mpu9250_esp_reading_t *out_reading);

/**
 * @brief Power down the AK8963 and the MPU-9250, then release the I2C bus
 *
 * The MPU-9250 is put into sleep mode with the clock stopped and the I2C bus,
 * both device handles and the context are freed.
 *
 * @param handle Handle from mpu9250_esp_init(); the context is freed, so the
 *               handle must not be used again
 * @return
 *  - ESP_OK: sensor powered down and resources released (a failed power-down
 *    register write is logged as a warning)
 *  - ESP_ERR_INVALID_ARG: handle is NULL
 *  - ESP_ERR_INVALID_STATE: handle is not in the initialized state
 */
esp_err_t mpu9250_esp_deinit(mpu9250_esp_handle_t handle);

#ifdef __cplusplus
}
#endif
