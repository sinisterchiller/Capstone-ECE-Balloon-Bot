/*
 * SPDX-FileCopyrightText: 2024 ECE Balloon Bot Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ESP-IDF SPI adapter for the Bosch BME68x Sensor API.
 *
 * Vendor source: boschsensortec/BME68x_SensorAPI v4.4.8,
 * commit 80ea120a8b8ac987d7d79eb68a9ed796736be845 (see README.md).
 * Vendor files (bme68x.c, bme68x.h, bme68x_defs.h, LICENSE) are unmodified.
 *
 * All register access, calibration and compensation remain inside the Bosch
 * driver; this component only supplies the ESP-IDF SPI transport, the sensor
 * setup and the translation to ESP-IDF types/error codes.
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "bme68x_defs.h"

/**
 * @brief BME68x SPI GPIO pin configuration (4-wire SPI, CS driven by the SPI driver)
 */
typedef struct {
    int mosi_gpio;   /*!< MOSI (host -> sensor), default GPIO23 */
    int miso_gpio;   /*!< MISO (sensor -> host), default GPIO19 */
    int sclk_gpio;   /*!< SCLK, default GPIO18 */
    int cs_gpio;     /*!< CS, default GPIO27 */
} bme68x_spi_pins_t;

/**
 * @brief BME68x sensor configuration (forced mode, single gas heater profile)
 *
 * The heater temperature/duration are applied by the Bosch API for the
 * BME68X_FORCED_MODE gas measurement performed by bme68x_esp_measure().
 */
typedef struct {
    uint8_t os_temp;       /*!< Temperature oversampling (default: BME68X_OS_2X) */
    uint8_t os_pres;       /*!< Pressure oversampling (default: BME68X_OS_4X) */
    uint8_t os_hum;        /*!< Humidity oversampling (default: BME68X_OS_2X) */
    uint8_t filter;        /*!< IIR filter coefficient (default: BME68X_FILTER_SIZE_3) */
    uint8_t odr;           /*!< Standby time, unused in forced mode (default: BME68X_ODR_NONE) */

    uint16_t heatr_temp;   /*!< Heater plate temperature in degrees C, 200..400 (default: 320) */
    uint16_t heatr_dur;    /*!< Heater on-time in ms, 1..4032 (default: 150) */
} bme68x_sensor_config_t;

/**
 * @brief One compensated BME68x measurement
 *
 * Temperature, pressure and humidity are always populated on ESP_OK. Gas
 * resistance is only meaningful when gas_valid is true; otherwise it is 0.
 * heat_stable reports whether the heater plate reached heatr_temp, so callers
 * that need metrologically valid gas data should require both flags.
 */
typedef struct {
    float temperature;     /*!< Temperature in degrees Celsius */
    float pressure;        /*!< Barometric pressure in Pascals */
    float humidity;        /*!< Relative humidity in %RH (internal/enclosure air) */
    float gas_resistance;  /*!< Gas resistance in Ohms; 0 when gas_valid is false */

    uint8_t gas_index;     /*!< Heater profile index used for this measurement */
    uint8_t meas_index;    /*!< Measurement index from the sensor */
    bool gas_valid;        /*!< Gas measurement valid (BME68X_GASM_VALID_MSK) */
    bool heat_stable;      /*!< Heater plate reached temperature (BME68X_HEAT_STAB_MSK) */

    int64_t timestamp_us;  /*!< esp_timer timestamp (us since boot) when data was read */
} bme68x_measurement_t;

/**
 * @brief BME68x device handle (opaque)
 */
typedef struct bme68x_esp_dev_t *bme68x_esp_handle_t;

/**
 * @brief Default SPI pin mapping (GPIO 18/19/23/27 per task spec)
 */
#define BME68X_SPI_PINS_DEFAULT() \
    {                             \
        .mosi_gpio = 23,          \
        .miso_gpio = 19,          \
        .sclk_gpio = 18,          \
        .cs_gpio = 27             \
    }

/**
 * @brief Default sensor configuration (conservative for balloon payload)
 * - Forced mode, moderate oversampling, IIR filter enabled
 * - Heater: 320 deg C for 150 ms (typical Bosch gas profile)
 */
#define BME68X_SENSOR_CONFIG_DEFAULT()      \
    {                                       \
        .os_temp = BME68X_OS_2X,            \
        .os_pres = BME68X_OS_4X,            \
        .os_hum = BME68X_OS_2X,             \
        .filter = BME68X_FILTER_SIZE_3,     \
        .odr = BME68X_ODR_NONE,             \
        .heatr_temp = 320,                  \
        .heatr_dur = 150                    \
    }

/**
 * @brief Initialize the BME68x over SPI on SPI2_HOST at 1 MHz, SPI mode 0
 *
 * Runs bme68x_init(), applies the TPH oversampling/filter configuration and the
 * gas heater profile. The conversion window used by bme68x_esp_measure() is
 * derived from bme68x_get_meas_dur() (using the configuration read back from the
 * sensor, i.e. after clamping) plus the heater duration.
 *
 * @param pins       SPI GPIO configuration (use BME68X_SPI_PINS_DEFAULT())
 * @param config     Sensor configuration (use BME68X_SENSOR_CONFIG_DEFAULT())
 * @param out_handle Output handle; set to NULL on failure
 *
 * @return
 *  - ESP_OK: sensor initialized
 *  - ESP_ERR_INVALID_ARG: NULL argument, negative GPIO, or heater value out of range
 *  - ESP_ERR_NO_MEM: device context allocation failed
 *  - ESP_ERR_INVALID_STATE: SPI2_HOST is already initialized by another driver
 *  - ESP_ERR_NOT_FOUND: no BME68x answered on the bus (BME68X_E_DEV_NOT_FOUND)
 *  - ESP_ERR_INVALID_RESPONSE: SPI transfer failed (BME68X_E_COM_FAIL)
 *  - ESP_FAIL / other: Bosch API rejected the configuration
 *
 * On failure the SPI device and bus are released and the context is freed.
 * The sensor must be accessed from a single task.
 */
esp_err_t bme68x_esp_init(const bme68x_spi_pins_t *pins,
                          const bme68x_sensor_config_t *config,
                          bme68x_esp_handle_t *out_handle);

/**
 * @brief Soft-reset the sensor and release the SPI device, bus and context
 *
 * @param handle Handle from bme68x_esp_init(); must not be used afterwards
 * @return ESP_OK (a failed soft reset only logs a warning)
 */
esp_err_t bme68x_esp_deinit(bme68x_esp_handle_t handle);

/**
 * @brief Perform a single forced-mode measurement of T, RH, P and gas
 *
 * Triggers forced mode, waits out the TPH conversion plus heater window and then
 * polls bme68x_get_data() (25 ms margin) until new data is available. Waiting
 * before the first read is required by the sensor's new_data/field-0 semantics:
 * otherwise a conversion that finished after a previous call returned would be
 * reported instead of the one this call triggered. Returns the Bosch-compensated
 * values in SI-style units.
 *
 * @param handle   Handle from bme68x_esp_init()
 * @param out_data Output measurement (zeroed before acquisition)
 *
 * @return
 *  - ESP_OK: measurement acquired; check out_data->gas_valid and
 *            out_data->heat_stable before using gas_resistance
 *  - ESP_ERR_INVALID_ARG: NULL argument
 *  - ESP_ERR_INVALID_STATE: handle not initialized
 *  - ESP_ERR_TIMEOUT: no new data within 25 ms after the conversion+heater window
 *  - ESP_ERR_INVALID_RESPONSE: SPI transfer failed (BME68X_E_COM_FAIL)
 *  - ESP_ERR_NOT_FOUND: sensor no longer responds (BME68X_E_DEV_NOT_FOUND)
 *  - ESP_ERR_INVALID_SIZE: unexpected register length (BME68X_E_INVALID_LENGTH)
 *  - ESP_FAIL: other Bosch API error
 */
esp_err_t bme68x_esp_measure(bme68x_esp_handle_t handle, bme68x_measurement_t *out_data);

/**
 * @brief Get the underlying Bosch API device structure (advanced use)
 *
 * @param handle Device handle
 * @return struct bme68x_dev* Pointer to the internal device structure, or NULL
 */
struct bme68x_dev *bme68x_esp_get_dev(bme68x_esp_handle_t handle);

/**
 * @brief Convert a Bosch API return code to a string
 *
 * @param rslt Bosch API return code (int8_t)
 * @return const char* Human-readable error string
 */
const char *bme68x_esp_strerror(int8_t rslt);

#ifdef __cplusplus
}
#endif
