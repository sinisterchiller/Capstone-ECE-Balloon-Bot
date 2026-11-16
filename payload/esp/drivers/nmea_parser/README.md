# NMEA Parser Component

## Provenance

This component is derived from the official ESP-IDF v5.5.2 example:
- **Source path**: `$HOME/.espressif/v5.5.2/esp-idf/examples/peripherals/uart/nmea0183_parser/main/`
- **Tag**: `v5.5.2` (via `git describe v5.5.2`)
- **Files copied**:
  - `nmea_parser.c` — parser implementation
  - `nmea_parser.h` — public API and types
  - `Kconfig.projbuild` — configuration options (adapted)

## Adaptations

1. **Componentized**: Removed the example `nmea_parser_example_main.c` and registered as a proper ESP-IDF component via `CMakeLists.txt` with `REQUIRES esp_event esp_driver_uart`.
2. **Default RX pin**: Changed `CONFIG_NMEA_PARSER_UART_RXD` default from `5` to `26` to match the NEO-6M TX→GPIO26 hardware wiring (ESP32-WROOM, common GND, 3.3 V GPS breakout).
3. **License headers**: Preserved original SPDX headers (Apache-2.0 for `.c`, Apache-2.0 for `.h`).
4. **No parser logic changes**: UART handling, NMEA decoding, CRC verification, event posting, and task logic are unmodified from the v5.5.2 source.

## Configuration

Key Kconfig options (all prefixed `NMEA_PARSER_` or `NMEA_STATEMENT_`):
- `NMEA_PARSER_UART_RXD` (default `26`) — UART RX GPIO
- `NMEA_PARSER_RING_BUFFER_SIZE` (default `1024`)
- `NMEA_PARSER_TASK_STACK_SIZE` (default `3072`)
- `NMEA_PARSER_TASK_PRIORITY` (default `2`)
- Statement enables: `GGA`, `GSA`, `GSV`, `RMC`, `GLL`, `VTG` (all default `y`)

## Usage

```c
#include "nmea_parser.h"

void gps_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data) {
    if (id == GPS_UPDATE) {
        gps_t *gps = (gps_t *)data;
        // Use gps->latitude, gps->longitude, etc.
    }
}

nmea_parser_config_t config = NMEA_PARSER_CONFIG_DEFAULT();
// Optionally override config.uart.rx_pin, config.uart.baud_rate, etc.
nmea_parser_handle_t hdl = nmea_parser_init(&config);
nmea_parser_add_handler(hdl, gps_event_handler, NULL);
// ...
nmea_parser_deinit(hdl);
```

The default configuration (`NMEA_PARSER_CONFIG_DEFAULT()`) uses:
- UART_NUM_1
- RX pin = `CONFIG_NMEA_PARSER_UART_RXD` (26)
- 9600 baud, 8N1
- Event queue size = 16

## Hardware Notes

- NEO-6M module: TX → ESP32 GPIO26, common GND, 3.3 V supply.
- Factory NMEA defaults assumed: 9600 8N1, 1 Hz, GGA/GSA/GSV/RMC/GLL/VTG enabled.
- The parser does not configure the GPS module; it only receives and decodes NMEA sentences.