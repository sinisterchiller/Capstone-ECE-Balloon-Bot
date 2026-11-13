/**
 * @brief GPS adapter implementation
 *
 * Wraps Espressif's NMEA parser component, subscribes to GPS_UPDATE events,
 * and exposes the latest position safely across tasks.
 */

#include "gps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_event.h"

static const char *TAG = "gps_adapter";

struct gps_adapter_t {
    nmea_parser_handle_t parser_handle;
    SemaphoreHandle_t mutex;
    gps_position_t latest;
    bool has_data;
    TickType_t last_update_tick;  /*!< FreeRTOS tick count of last GPS_UPDATE */
};

/* Forward declaration for the event handler */
static void gps_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);

esp_err_t gps_init(gps_handle_t *out_handle) {
    if (!out_handle) {
        return ESP_ERR_INVALID_ARG;
    }

    gps_adapter_t *adapter = (gps_adapter_t *)calloc(1, sizeof(gps_adapter_t));
    if (!adapter) {
        ESP_LOGE(TAG, "Failed to allocate GPS adapter");
        return ESP_ERR_NO_MEM;
    }

    adapter->mutex = xSemaphoreCreateMutex();
    if (!adapter->mutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        free(adapter);
        return ESP_ERR_NO_MEM;
    }

    adapter->has_data = false;
    adapter->last_update_tick = 0;

    /* Use default parser configuration */
    nmea_parser_config_t config = NMEA_PARSER_CONFIG_DEFAULT();
    adapter->parser_handle = nmea_parser_init(&config);
    if (!adapter->parser_handle) {
        ESP_LOGE(TAG, "Failed to initialize NMEA parser");
        vSemaphoreDelete(adapter->mutex);
        free(adapter);
        return ESP_FAIL;
    }

    /* Register event handler for GPS updates */
    esp_err_t err = nmea_parser_add_handler(adapter->parser_handle, gps_event_handler, adapter);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add NMEA parser handler: %s", esp_err_to_name(err));
        nmea_parser_deinit(adapter->parser_handle);
        vSemaphoreDelete(adapter->mutex);
        free(adapter);
        return err;
    }

    ESP_LOGI(TAG, "GPS adapter initialized");
    *out_handle = adapter;
    return ESP_OK;
}

esp_err_t gps_deinit(gps_handle_t handle) {
    if (!handle) {
        return ESP_ERR_INVALID_ARG;
    }

    gps_adapter_t *adapter = (gps_adapter_t *)handle;

    /* Remove event handler before deinit */
    if (adapter->parser_handle) {
        esp_err_t err = nmea_parser_remove_handler(adapter->parser_handle, gps_event_handler);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to remove NMEA parser handler: %s", esp_err_to_name(err));
        }
        nmea_parser_deinit(adapter->parser_handle);
    }

    vSemaphoreDelete(adapter->mutex);
    free(adapter);
    ESP_LOGI(TAG, "GPS adapter deinitialized");
    return ESP_OK;
}

bool gps_get_latest(gps_handle_t handle, gps_position_t *out_position) {
    if (!handle || !out_position) {
        return false;
    }

    gps_adapter_t *adapter = (gps_adapter_t *)handle;
    TickType_t now = xTaskGetTickCount();

    if (xSemaphoreTake(adapter->mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        bool result = adapter->has_data;
        if (result) {
            /* Check if the fix is stale */
            TickType_t age = now - adapter->last_update_tick;
            if (age > pdMS_TO_TICKS(GPS_FIX_TIMEOUT_MS)) {
                result = false;  /* Expire stale fix */
            } else {
                *out_position = adapter->latest;
            }
        }
        xSemaphoreGive(adapter->mutex);
        return result;
    }

    return false;
}

bool gps_has_fix(gps_handle_t handle) {
    gps_position_t pos;
    if (!gps_get_latest(handle, &pos)) {
        return false;
    }
    return (pos.fix != GPS_FIX_INVALID) && pos.valid;
}

static void gps_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    gps_adapter_t *adapter = (gps_adapter_t *)arg;

    if (event_base != ESP_NMEA_EVENT) {
        return;
    }

    if (event_id == GPS_UPDATE) {
        const gps_t *parser_data = (const gps_t *)event_data;
        if (!parser_data) {
            return;
        }

        /* Copy relevant fields under mutex protection */
        if (xSemaphoreTake(adapter->mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            adapter->latest.latitude = parser_data->latitude;
            adapter->latest.longitude = parser_data->longitude;
            adapter->latest.altitude = parser_data->altitude;
            adapter->latest.fix = parser_data->fix;
            adapter->latest.valid = parser_data->valid;
            adapter->latest.sats_in_use = parser_data->sats_in_use;
            adapter->latest.tim = parser_data->tim;
            adapter->has_data = true;
            adapter->last_update_tick = xTaskGetTickCount();
            xSemaphoreGive(adapter->mutex);
        }
    }
    /* GPS_UNKNOWN events are intentionally ignored for this minimal adapter */
}