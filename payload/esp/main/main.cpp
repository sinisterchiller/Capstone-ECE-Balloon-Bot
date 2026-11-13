/**
 * @brief Balloon payload GPS + BME688 + MPU-9250 demonstration
 *
 * Initializes the GPS adapter and logs position updates, samples the BME688
 * over SPI and the MPU-9250 over I2C in the same loop. GPS behavior is
 * unchanged: process() runs every iteration and blocks for one second, so the
 * sensor sampling periods are counted in FreeRTOS ticks on top of that. A
 * missing or failing BME688 or MPU-9250 logs errors but never stops the GPS
 * loop or the other sensor.
 */

#include <cstdint>

#include "bme.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gps.h"
#include "mpu.h"

static const char *TAG = "gps_demo";
static const char *BME_TAG = "bme_app";
static const char *MPU_TAG = "mpu_app";

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

/**
 * @brief MPU-9250 sampling period
 *
 * The GPS process() call blocks for about one second per loop iteration, so one
 * reading per iteration is the effective rate; the interval bounds it in case a
 * future loop iteration gets faster. Polling at this rate keeps the 100 kHz I2C
 * bus and the log quiet (no interrupt pin, FIFO or DMP is used).
 */
static constexpr TickType_t MPU_SAMPLE_INTERVAL = pdMS_TO_TICKS(1000);

/**
 * @brief Log a repeated MPU-9250 error at most once per minute
 *
 * With the 1 s sampling period, sixty consecutive identical failures are about
 * one minute of logging silence.
 */
static constexpr uint32_t MPU_ERROR_LOG_EVERY = 60;

/** @brief Tracks MPU-9250 failures so a broken sensor does not flood the log */
struct MpuErrorThrottle {
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

static void log_mpu_reading(const MpuReading &reading) {
    ESP_LOGI(MPU_TAG, "a=[%.3f %.3f %.3f] g w=[%.2f %.2f %.2f] dps m=[%.2f %.2f %.2f] uT",
             reading.accel_g[0], reading.accel_g[1], reading.accel_g[2],
             reading.gyro_dps[0], reading.gyro_dps[1], reading.gyro_dps[2],
             reading.mag_ut[0], reading.mag_ut[1], reading.mag_ut[2]);
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

static void report_mpu_status(MpuErrorThrottle &throttle, esp_err_t err) {
    if (err == ESP_OK) {
        if (throttle.last_err != ESP_OK) {
            ESP_LOGI(MPU_TAG, "MPU-9250 recovered after repeated %s failures",
                     esp_err_to_name(throttle.last_err));
            throttle.last_err = ESP_OK;
            throttle.repeats = 0;
        }
        return;
    }

    if (err != throttle.last_err) {
        ESP_LOGE(MPU_TAG, "MPU-9250 read failed: %s; GPS and BME688 continue",
                 esp_err_to_name(err));
        throttle.last_err = err;
        throttle.repeats = 0;
    } else if (++throttle.repeats >= MPU_ERROR_LOG_EVERY) {
        ESP_LOGW(MPU_TAG, "MPU-9250 still failing: %s (%u consecutive reads)",
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

    /* The MPU-9250 is independent of the other sensors: its failure leaves the
     * GPS loop and the BME688 sampling untouched. */
    Mpu mpu;
    err = mpu.init();
    if (err != ESP_OK) {
        ESP_LOGE(MPU_TAG, "MPU-9250 initialization failed: %s; continuing with GPS and BME688 only",
                 esp_err_to_name(err));
    }

    BmeErrorThrottle bme_throttle;
    TickType_t next_bme_tick = xTaskGetTickCount();

    MpuErrorThrottle mpu_throttle;
    TickType_t next_mpu_tick = xTaskGetTickCount();

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

        if (mpu.is_initialized() &&
            (xTaskGetTickCount() - next_mpu_tick) >= MPU_SAMPLE_INTERVAL) {
            next_mpu_tick += MPU_SAMPLE_INTERVAL;

            MpuReading reading;
            err = mpu.read(&reading);
            if (err == ESP_OK) {
                log_mpu_reading(reading);
            }
            report_mpu_status(mpu_throttle, err);
        }
    }
}
