/**
 * @brief Minimal MPU-9250 interface for the balloon payload
 *
 * Thin application-facing adapter over the drivers/mpu9250 ESP-IDF component.
 * I2C ownership, register access, scaling and AK8963 handling all stay inside
 * drivers/mpu9250/; this interface only exposes the reading and the units the
 * application logs.
 */

#pragma once

#ifdef __cplusplus

#include "esp_err.h"
#include "mpu9250_esp.h"

/**
 * @brief One nine-axis MPU-9250 reading in application units
 *
 * The adapter type already carries the units the application logs: accel_g in
 * g, gyro_dps in degrees per second, mag_ut in microtesla and timestamp_us as
 * an esp_timer timestamp (us since boot). All nine axes are valid on ESP_OK.
 */
using MpuReading = mpu9250_esp_reading_t;

/**
 * @brief C++ MPU-9250 wrapper for object-oriented usage
 *
 * Initializes the sensor on the adapter's fixed I2C wiring (SDA GPIO21,
 * SCL GPIO22, 100 kHz, address 0x68) with the adapter's default configuration
 * (50 Hz, +-2 g, +-2000 dps, AK8963 16-bit continuous). Sampling is polling
 * only: no interrupt pin, FIFO or DMP. The sensor is not thread-safe; use it
 * from a single task. Non-copyable: stack-only use in app_main.
 */
class Mpu {
public:
    Mpu() = default;
    ~Mpu() { deinit(); }

    // Non-copyable, non-movable (stack-only in app_main)
    Mpu(const Mpu&) = delete;
    Mpu& operator=(const Mpu&) = delete;
    Mpu(Mpu&&) = delete;
    Mpu& operator=(Mpu&&) = delete;

    /**
     * @brief Initialize the I2C bus and the sensor
     * @return ESP_OK on success, error code on failure (no sensor on the bus,
     *         I2C bus unavailable, configuration rejected)
     */
    esp_err_t init();

    /**
     * @brief Acquire one nine-axis sample (blocking I2C reads, no conversion wait)
     * @param[out] out_reading Filled on ESP_OK only
     * @return ESP_OK on success, error code on failure
     */
    esp_err_t read(MpuReading *out_reading);

    /**
     * @brief Power the sensor down and release the I2C bus
     * @return ESP_OK on success (also when not initialized)
     */
    esp_err_t deinit();

    /** @brief True after a successful init() and before deinit() */
    bool is_initialized() const { return handle_ != nullptr; }

private:
    mpu9250_esp_handle_t handle_ = nullptr;
};

#endif  // __cplusplus
