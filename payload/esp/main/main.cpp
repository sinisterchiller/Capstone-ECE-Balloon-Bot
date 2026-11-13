/**
 * @brief Balloon payload GPS + BME688 demonstration
 *
 * Initializes the GPS adapter and logs position updates, and samples the
 * BME688 over SPI in the same loop. GPS behavior is unchanged: process() runs
 * every iteration and blocks for one second, so the BME688 sampling period is
 * counted in FreeRTOS ticks on top of that. A missing or failing BME688 logs
 * errors but never stops the GPS loop.
 */

#include <cstdint>

#include "bme.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gps.h"

static const char *TAG = "gps_demo";
static const char *BME_TAG = "bme_app";

/**
 * @brief BME688 sampling period
 *
 * Five seconds is an application choice for the balloon telemetry rate, not a
 * hardware-validated value: one forced-mode conversion with the driver's
 * default heater profile (320 degC / 150 ms) completes well inside one GPS
 * loop iteration (~1 s), and 5 s keeps the gas heater and the air around the
 * sensor settled between readings.
 */
static constexpr TickType_t BME_SAMPLE_INTERVAL = pdMS_TO_TICKS(5000);

/**
 * @brief Log a repeated BME688 error at most once per minute
 *
 * With the 5 s sampling period, twelve consecutive identical failures are
 * about one minute of logging silence.
 */
static constexpr uint32_t BME_ERROR_LOG_EVERY = 12;

/** @brief Tracks BME688 failures so a broken sensor does not flood the log */
struct BmeErrorThrottle {
    esp_err_t last_err = ESP_OK; /*!< Last reported error; ESP_OK means healthy */
    uint32_t repeats = 0;        /*!< Consecutive occurrences of last_err */
};

static void log_bme_reading(const BmeReading &reading) {
    if (reading.gas_valid && reading.heat_stable) {
        ESP_LOGI(BME_TAG, "T=%.2f C RH=%.2f %% (internal/enclosure) P=%.0f Pa gas=%.0f Ohm",
                 reading.temperature_c, reading.humidity_pct, reading.pressure_pa,
                 reading.gas_resistance_ohm);
    } else {
        ESP_LOGW(BME_TAG,
                 "T=%.2f C RH=%.2f %% (internal/enclosure) P=%.0f Pa gas invalid "
                 "(gas_valid=%d heat_stable=%d), resistance discarded",
                 reading.temperature_c, reading.humidity_pct, reading.pressure_pa,
                 reading.gas_valid, reading.heat_stable);
    }
}

static void report_bme_status(BmeErrorThrottle &throttle, esp_err_t err) {
    if (err == ESP_OK) {
        if (throttle.last_err != ESP_OK) {
            ESP_LOGI(BME_TAG, "BME688 recovered after repeated %s failures",
                     esp_err_to_name(throttle.last_err));
            throttle.last_err = ESP_OK;
            throttle.repeats = 0;
        }
        return;
    }

    if (err != throttle.last_err) {
        ESP_LOGE(BME_TAG, "BME688 read failed: %s; GPS loop continues",
                 esp_err_to_name(err));
        throttle.last_err = err;
        throttle.repeats = 0;
    } else if (++throttle.repeats >= BME_ERROR_LOG_EVERY) {
        ESP_LOGW(BME_TAG, "BME688 still failing: %s (%u consecutive reads)",
                 esp_err_to_name(err), (unsigned)throttle.repeats);
        throttle.repeats = 0;
    }
}

extern "C" void app_main(void) {
    Gps gps;
    esp_err_t err = gps.init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "GPS initialization failed: %s", esp_err_to_name(err));
        return;
    }

    /* Sensor failure degrades the payload to GPS-only; it is not fatal. */
    Bme bme;
    err = bme.init();
    if (err != ESP_OK) {
        ESP_LOGE(BME_TAG, "BME688 initialization failed: %s; continuing with GPS only",
                 esp_err_to_name(err));
    }

    BmeErrorThrottle bme_throttle;
    TickType_t next_bme_tick = xTaskGetTickCount();

    while (true) {
        gps.process(); /* blocks for one second */

        if (bme.is_initialized() &&
            (xTaskGetTickCount() - next_bme_tick) >= BME_SAMPLE_INTERVAL) {
            next_bme_tick += BME_SAMPLE_INTERVAL;

            BmeReading reading;
            err = bme.read(&reading);
            if (err == ESP_OK) {
                log_bme_reading(reading);
            }
            report_bme_status(bme_throttle, err);
        }
    }
}
