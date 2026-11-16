/**
 * @brief Minimal MPU-6500 interface for the balloon payload
 *
 * Thin application-facing adapter over the drivers/mpu6500 ESP-IDF component.
 * I2C ownership, register access and scaling all stay inside drivers/mpu6500/;
 * this interface only exposes the reading and the units the application logs.
 * The MPU-6500 is a six-axis part, so no magnetometer reading is exposed.
 */

#pragma once

#ifdef __cplusplus

#include "esp_err.h"
#include "mpu6500_esp.h"

/**
 * @brief One six-axis MPU-6500 reading in application units
 *
 * The adapter type already carries the units the application logs: accel_g in
 * g, gyro_dps in degrees per second and timestamp_us as an esp_timer timestamp
 * (us since boot). Both axis triplets are valid on ESP_OK.
 */
using MpuReading = mpu6500_esp_reading_t;

/**
 * @brief C++ MPU-6500 wrapper for object-oriented usage
 *
 * Initializes the sensor on the adapter's fixed I2C wiring (SDA GPIO21,
 * SCL GPIO22, 100 kHz, address 0x68) with the adapter's default configuration
 * (50 Hz, +-2 g, +-2000 dps). Sampling is polling only: no interrupt pin, FIFO
 * or DMP. The sensor is not thread-safe; use it from a single task.
 * Non-copyable: stack-only use in app_main.
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
     * @return ESP_OK on success, error code on failure (no MPU-6500 on the bus,
     *         I2C bus unavailable, configuration rejected)
     */
    esp_err_t init();

    /**
     * @brief Acquire one six-axis sample (blocking I2C reads, no conversion wait)
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
    mpu6500_esp_handle_t handle_ = nullptr;
};

#endif  // __cplusplus
