/**
 * @brief Balloon payload GPS demonstration
 *
 * Initializes the GPS adapter and logs position updates.
 * Replaces the Hello World example with minimal GPS integration.
 * Logs no-fix on startup and when fix is lost or stale.
 */

#include "gps.h"
#include "esp_log.h"

static const char *TAG = "gps_demo";

extern "C" void app_main(void) {
    Gps gps;
    esp_err_t err = gps.init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "GPS initialization failed: %s", esp_err_to_name(err));
        return;
    }

    while (true) {
        gps.process();
    }
}