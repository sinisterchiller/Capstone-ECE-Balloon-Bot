/**
 * @brief Minimal GPS interface for the balloon payload
 *
 * Thin adapter over Espressif's official NMEA parser component.
 * All UART and NMEA logic remains in drivers/nmea_parser/.
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "nmea_parser.h"
#include "esp_err.h"

/**
 * @brief Maximum age of a GPS fix before it is considered stale (milliseconds)
 *
 * Set to 10 seconds to exceed the 1 Hz NMEA update period with margin.
 * If no GPS_UPDATE event is received within this window, the fix is expired.
 */
#define GPS_FIX_TIMEOUT_MS 10000

/**
 * @brief Opaque handle for the GPS adapter
 */
typedef struct gps_adapter_t *gps_handle_t;

/**
 * @brief Snapshot of the latest GPS position and status
 *
 * Mirrors the parser's gps_t but exposed through a safe accessor.
 * Validity is determined by fix != GPS_FIX_INVALID and valid == true.
 */
typedef struct {
    float latitude;      /*!< Latitude in degrees */
    float longitude;     /*!< Longitude in degrees */
    float altitude;      /*!< Altitude in meters */
    gps_fix_t fix;       /*!< Fix status */
    bool valid;          /*!< Overall validity (from RMC/GLL) */
    uint8_t sats_in_use; /*!< Number of satellites used in fix */
    gps_time_t tim;      /*!< UTC time of fix */
} gps_position_t;

/**
 * @brief Initialize the GPS adapter and start the parser
 *
 * Uses the default parser configuration (UART1, 9600 8N1, RX on CONFIG_NMEA_PARSER_UART_RXD).
 * Must be called once before any other GPS function.
 *
 * @param[out] out_handle Handle for subsequent GPS operations
 * @return ESP_OK on success, error code on failure
 */
esp_err_t gps_init(gps_handle_t *out_handle);

/**
 * @brief Deinitialize the GPS adapter and stop the parser
 *
 * @param handle Handle returned by gps_init
 * @return ESP_OK on success, error code on failure
 */
esp_err_t gps_deinit(gps_handle_t handle);

/**
 * @brief Get the latest GPS position snapshot
 *
 * Thread-safe: copies the most recent parser update under a mutex.
 * Returns false if no update has been received yet or if the fix is stale
 * (older than GPS_FIX_TIMEOUT_MS).
 *
 * @param handle Handle returned by gps_init
 * @param[out] out_position Filled with latest position data
 * @return true if a fresh position snapshot is available, false otherwise
 */
bool gps_get_latest(gps_handle_t handle, gps_position_t *out_position);

/**
 * @brief Check if the GPS currently has a valid fix
 *
 * Considers fix validity and freshness (not older than GPS_FIX_TIMEOUT_MS).
 *
 * @param handle Handle returned by gps_init
 * @return true if valid and fresh fix, false otherwise
 */
bool gps_has_fix(gps_handle_t handle);

#ifdef __cplusplus
}
#endif