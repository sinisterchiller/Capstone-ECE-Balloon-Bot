/**
 * @brief Minimal BME688 interface for the balloon payload
 *
 * Thin application-facing adapter over the drivers/bme68x ESP-IDF component.
 * SPI transport, register access, calibration and the Bosch compensation
 * algorithms all stay inside drivers/bme68x/; this interface only names the
 * readings and the units the application logs.
 */

#pragma once

#ifdef __cplusplus

#include <cstdint>

#include "bme68x_esp.h"
#include "esp_err.h"

/**
 * @brief One compensated BME688 reading in application units
 *
 * gas_resistance_ohm is only meaningful when gas_valid and heat_stable are
 * both true; the driver reports 0 for an invalid gas measurement.
 */
struct BmeReading {
    float temperature_c;      /*!< Temperature in degrees Celsius */
    float humidity_pct;       /*!< Relative humidity in %RH (internal/enclosure air) */
    float pressure_pa;        /*!< Barometric pressure in Pascals */
    float gas_resistance_ohm; /*!< Gas resistance in Ohms; 0 unless gas_valid */
    bool gas_valid;           /*!< Sensor flagged the gas measurement valid */
    bool heat_stable;         /*!< Heater plate reached the configured temperature */
    int64_t timestamp_us;     /*!< esp_timer timestamp (us since boot) of the reading */
};

/**
 * @brief C++ BME688 wrapper for object-oriented usage
 *
 * Initializes the sensor on the driver's default SPI pin mapping and default
 * forced-mode configuration (see BME68X_SPI_PINS_DEFAULT() and
 * BME68X_SENSOR_CONFIG_DEFAULT()). The sensor is not thread-safe; use it from
 * a single task. Non-copyable: stack-only use in app_main.
 */
class Bme {
public:
    Bme() = default;
    ~Bme() { deinit(); }

    // Non-copyable, non-movable (stack-only in app_main)
    Bme(const Bme&) = delete;
    Bme& operator=(const Bme&) = delete;
    Bme(Bme&&) = delete;
    Bme& operator=(Bme&&) = delete;

    /**
     * @brief Initialize SPI and the sensor with the driver defaults
     * @return ESP_OK on success, error code on failure (no sensor on the bus,
     *         SPI bus unavailable, configuration rejected)
     */
    esp_err_t init();

    /**
     * @brief Acquire one measurement (blocking for the conversion + heater time)
     * @param[out] out_reading Filled on ESP_OK only
     * @return ESP_OK on success, error code on failure
     */
    esp_err_t read(BmeReading *out_reading);

    /**
     * @brief Soft-reset the sensor and release the SPI bus
     * @return ESP_OK on success (also when not initialized)
     */
    esp_err_t deinit();

    /** @brief True after a successful init() and before deinit() */
    bool is_initialized() const { return handle_ != nullptr; }

private:
    bme68x_esp_handle_t handle_ = nullptr;
};

#endif  // __cplusplus
