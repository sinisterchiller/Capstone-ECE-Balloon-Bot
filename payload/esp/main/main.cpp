/**
 * @brief Balloon payload GPS demonstration
 *
 * Initializes the GPS adapter and logs position updates.
 * Replaces the Hello World example with minimal GPS integration.
 * Logs no-fix on startup and when fix is lost or stale.
 */

#include "gps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "gps_demo";

extern "C" void app_main(void) {
    gps_handle_t gps = nullptr;
    esp_err_t err = gps_init(&gps);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "GPS initialization failed: %s", esp_err_to_name(err));
        return;
    }

    ESP_LOGI(TAG, "GPS demo started, waiting for updates...");

    gps_position_t last_logged = {};
    enum class LogState { NONE, VALID_FIX, NO_FIX };
    LogState last_log_state = LogState::NONE;
    TickType_t last_log_time = 0;
    const TickType_t LOG_THROTTLE_MS = pdMS_TO_TICKS(5000);        /* Throttle repeated valid logs to 5s */
    const TickType_t NO_FIX_LOG_INTERVAL_MS = pdMS_TO_TICKS(30000); /* Periodic no-fix logs */

    while (true) {
        gps_position_t pos;
        bool has_fresh_data = gps_get_latest(gps, &pos);
        bool has_fix = has_fresh_data && (pos.fix != GPS_FIX_INVALID) && pos.valid;

        if (has_fix) {
            TickType_t now = xTaskGetTickCount();
            bool position_changed = (pos.latitude != last_logged.latitude) ||
                                    (pos.longitude != last_logged.longitude) ||
                                    (pos.altitude != last_logged.altitude) ||
                                    (pos.sats_in_use != last_logged.sats_in_use);
            bool throttle_expired = (now - last_log_time) >= LOG_THROTTLE_MS;
            bool state_transition = last_log_state != LogState::VALID_FIX;

            if (state_transition || position_changed || throttle_expired) {
                ESP_LOGI(TAG,
                         "Valid fix: lat=%.6f lon=%.6f alt=%.1f fix=%d sats=%u time=%02u:%02u:%02u",
                         pos.latitude, pos.longitude, pos.altitude,
                         pos.fix, pos.sats_in_use,
                         pos.tim.hour, pos.tim.minute, pos.tim.second);
                last_logged = pos;
                last_log_state = LogState::VALID_FIX;
                last_log_time = now;
            }
        } else {
            /* No fresh data or no valid fix */
            TickType_t now = xTaskGetTickCount();
            bool state_transition = last_log_state != LogState::NO_FIX;
            bool interval_expired = (now - last_log_time) >= NO_FIX_LOG_INTERVAL_MS;

            if (state_transition || interval_expired) {
                if (has_fresh_data) {
                    ESP_LOGI(TAG, "No valid fix (fix=%d, valid=%d, sats=%u)", pos.fix, pos.valid, pos.sats_in_use);
                } else {
                    ESP_LOGI(TAG, "No valid fix (no GPS data received yet or fix stale)");
                }
                last_log_state = LogState::NO_FIX;
                last_log_time = now;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    /* Unreachable in normal operation, but kept for completeness */
    gps_deinit(gps);
}