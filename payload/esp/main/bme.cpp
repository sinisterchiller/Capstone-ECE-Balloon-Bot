/**
 * @brief BME688 adapter implementation
 *
 * Forwards to the drivers/bme68x component. No SPI, register, calibration or
 * compensation code lives here.
 */

#include "bme.h"

esp_err_t Bme::init() {
    if (handle_ != nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    const bme68x_spi_pins_t pins = BME68X_SPI_PINS_DEFAULT();
    const bme68x_sensor_config_t config = BME68X_SENSOR_CONFIG_DEFAULT();
    return bme68x_esp_init(&pins, &config, &handle_);
}

esp_err_t Bme::read(BmeReading *out_reading) {
    if (out_reading == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    if (handle_ == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    bme68x_measurement_t measurement;
    esp_err_t err = bme68x_esp_measure(handle_, &measurement);
    if (err != ESP_OK) {
        return err;
    }

    out_reading->temperature_c = measurement.temperature;
    out_reading->humidity_pct = measurement.humidity;
    out_reading->pressure_pa = measurement.pressure;
    out_reading->gas_resistance_ohm = measurement.gas_resistance;
    out_reading->gas_valid = measurement.gas_valid;
    out_reading->heat_stable = measurement.heat_stable;
    out_reading->timestamp_us = measurement.timestamp_us;
    return ESP_OK;
}

esp_err_t Bme::deinit() {
    if (handle_ == nullptr) {
        return ESP_OK;
    }

    esp_err_t err = bme68x_esp_deinit(handle_);
    handle_ = nullptr;
    return err;
}
