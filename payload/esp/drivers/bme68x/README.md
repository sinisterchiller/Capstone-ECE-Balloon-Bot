# BME68x driver component (BME688, 4-wire SPI)

ESP-IDF component wrapping the Bosch BME68x Sensor API plus a thin SPI adapter.
It exposes compensated temperature, relative humidity, pressure and gas
resistance without BSEC/IAQ processing.

## Vendor provenance

| Item | Value |
| --- | --- |
| Repository | https://github.com/boschsensortec/BME68x_SensorAPI |
| Pinned commit | `80ea120a8b8ac987d7d79eb68a9ed796736be845` (2023-05-17, "Updated to v4.4.8") |
| API version | v4.4.8 (`@version` in `bme68x.c`, `bme68x.h`, `bme68x_defs.h`) |
| License | BSD-3-Clause, see `LICENSE` |
| Imported on | 2026-11-12 |

The vendor files are unmodified. Verified byte-identical to the pinned upstream
commit:

| File | SHA-256 |
| --- | --- |
| `bme68x.c` | `bad525ea57fe57a5d7dda19d287781bcbab50fdbf2dedea737c16b206cbe0157` |
| `bme68x.h` | `09ed6babf92955ced360790958e2e2f03efb85bc4f0a8a864a4151a905eaf0ca` |
| `bme68x_defs.h` | `5f59c0bafbcc416c091a1a1cc40bb7362f0b48ea02ac7fa5197813f491de01aa` |
| `LICENSE` | `56df116398e0b72a12ae353d93002d29175583590283ced2c2510e3d8075546f` |

Re-verify with:

```sh
B=https://raw.githubusercontent.com/boschsensortec/BME68x_SensorAPI/80ea120a8b8ac987d7d79eb68a9ed796736be845
for f in bme68x.c bme68x.h bme68x_defs.h LICENSE; do
  curl -sSL "$B/$f" | shasum -a 256
done
```

## Files

| File | Owner | Purpose |
| --- | --- | --- |
| `bme68x.c`, `bme68x.h`, `bme68x_defs.h`, `LICENSE` | Bosch | Sensor commands, calibration, compensation. Do not edit. |
| `bme68x_esp.c` | Project | SPI transport (read/write/delay callbacks), sensor setup, error translation |
| `bme68x_esp.h` | Project | Public adapter interface |
| `bme68x_spi_frame.c`, `bme68x_spi_frame.h` | Project | ESP-IDF-free frame construction, shared with the host test |
| `CMakeLists.txt` | Project | Component registration (`esp_driver_spi`, `esp_timer`) |

## Wiring and bus configuration

| BME688 pin | ESP32-WROOM GPIO |
| --- | --- |
| SCLK | 18 |
| MISO / SDO | 19 |
| MOSI / SDI | 23 |
| CS | 27 (driven by the SPI driver) |

`bme68x_esp_init()` uses `SPI2_HOST` at 1 MHz, SPI mode 0, full duplex, with
CPU-driven transfers (no DMA). It owns `SPI2_HOST`: `spi_bus_initialize()`
returns `ESP_ERR_INVALID_STATE` if another driver already initialized that bus,
and `bme68x_esp_deinit()` frees it again.

## Measurement contract

`bme68x_esp_measure()` triggers one forced-mode conversion, waits out the
conversion window (`bme68x_get_meas_dur()` for the TPH conversion **plus**
`heatr_dur` for the gas heater, rounded up to whole RTOS ticks) and only then
calls `bme68x_get_data()`, polling it for at most another 25 ms. It returns:

| Field | Unit | Notes |
| --- | --- | --- |
| `temperature` | °C | Bosch compensated |
| `pressure` | Pa | Bosch compensated |
| `humidity` | %RH | Internal/enclosure humidity (SHT45/BME688 are not rated for ambient −60 °C air) |
| `gas_resistance` | Ω | Only meaningful when `gas_valid`; 0 otherwise |
| `gas_valid` | bool | `BME68X_GASM_VALID_MSK` |
| `heat_stable` | bool | `BME68X_HEAT_STAB_MSK`; heater plate reached target |
| `timestamp_us` | µs | `esp_timer_get_time()` at read time |

`bme68x_defs.h` enables `BME68X_USE_FPU` unless the build defines
`BME68X_DO_NOT_USE_FPU`, so by default the vendor already returns °C, Pa and
%RH and the adapter forwards them unchanged (the `x1000` comment in
`bme68x_defs.h` for the float humidity field is stale). If `BME68X_DO_NOT_USE_FPU`
is defined, the API reports temperature as °C×100 and humidity as %RH×1000 and
the adapter converts both; that mode is compile-checked as well.

Invalid gas data is rejected rather than clamped or guessed: `gas_resistance` is
set to 0 and `gas_valid` is false, while T/RH/P from the same conversion remain
usable. Callers that need metrologically valid gas readings should require
`gas_valid && heat_stable`.

The conversion window is computed once at init from the configuration the sensor
actually holds: `bme68x_set_conf()` clamps out-of-range oversampling values, so
the adapter reads the configuration back with `bme68x_get_conf()` and feeds that
into `bme68x_get_meas_dur()`. The **wait before the first read** matters: the
datasheet states that forced mode uses only field 0 (5.3.5) and that `new_data`
marks data "not already read-out" (5.3.6.1), so a conversion that finished after
the previous call had already returned (for example following a timeout) leaves
`new_data` set. Reading immediately after the trigger would then return that
earlier measurement and leave the new one unread, shifting every subsequent
result by one trigger. Waiting out the window - the sequence used by Bosch's
`examples/forced_mode` - guarantees field 0 holds this call's result. The wait is
rounded up to whole ticks and never shortened.

Note that the vendor encodes `heatr_dur` into the `gas_wait` register as
`value * 4^factor * 0.477 ms`, which is always **≤** the requested `heatr_dur`
(150 ms becomes 70.6 ms), so the requested value is a safe upper bound for the
wait. `heatr_dur` is validated to 1–4032 (the encodable range) and `heatr_temp`
to 200–400 °C.

## SPI callback contract (why the framing looks like this)

* `read(reg_addr, ...)`: the Bosch layer already ORs `BME68X_SPI_RD_MSK` (0x80) into
  `reg_addr`. Data starts on the clocks after the address byte, so one full-duplex
  transfer of `len + 1` bytes is issued and `rx[1..len]` is copied out.
* `write(reg_addr, reg_data, len, ...)`: the Bosch layer passes the address already
  masked with `BME68X_SPI_WR_MSK` (0x7f) and `reg_data` holding the interleaved
  address/data stream. The whole frame `{addr, reg_data...}` is sent in a single
  CS-low transaction; splitting it per byte would raise CS between a register
  address and its data.
* Both callbacks are usable during `bme68x_init()` (they must not test an
  "initialized" flag, which only becomes true after init completes). Frames are
  built by `bme68x_spi_frame.c` and bounded by `BME68X_SPI_FRAME_MAX_BYTES` (64
  bytes, including the control byte): ESP32 CPU-driven SPI cannot move more than
  512 bits per transfer (`SPI_LL_CPU_MAX_BIT_LEN`), and a 65-byte frame would be
  rejected by the driver. The largest frame the Bosch API needs is 52 bytes (51
  register bytes for three field blocks).

## Host regression test

`bme68x_spi_frame.c` has no ESP-IDF dependencies, and the Bosch API is portable,
so both can be built with the host compiler against a mock BME688 register file
(`../tests/bme68x_host_test.c`). The test checks the read/write frame layout
(including the vendor-interleaved five-register write), the init/configuration
traffic, forced-mode compensation units in both the float and integer builds,
the gas status flags, and the `new_data` behaviour the wait above relies on:

```sh
tests/run_bme68x_host_test.sh
```

## Troubleshooting `bme68x_init failed: Device not found`

`bme68x_init()` soft-resets the sensor, reads the chip ID register (`0xD0`)
**once** and returns `BME68X_E_DEV_NOT_FOUND` for any value other than `0x61`
(`BME68X_CHIP_ID`); the adapter maps that to `ESP_ERR_NOT_FOUND`. There are no
retries and the bus configuration is not changed, so a failure means the byte
on MISO was not the BME688 chip ID - it does **not** by itself say why. The
adapter therefore logs the evidence it has, at `ESP_LOGE` (`bme68x_esp` tag):

```
E bme68x_esp: bme68x_init failed: Device not found (rslt=-3)
E bme68x_esp: Chip ID reg 0xD0 returned 0xFF, expected 0x61
E bme68x_esp: Bus used: SPI2_HOST MOSI=GPIO23 MISO=GPIO19 SCLK=GPIO18 CS=GPIO27, mode 0, 1000000 Hz; last callback results read=0 write=0 (0 = transferred)
E bme68x_esp: 0xFF = MISO read high on all 8 clocks, ...   <- example: MISO silent
E bme68x_esp: Immediate re-read returned 0xFF (stable, same value)
```

`read=`/`write=` are the adapter's SPI callback results for `bme68x_init`'s
soft-reset write and chip ID read: `0` (`BME68X_OK`) means the
`spi_device_polling_transmit()` transfer completed, so the bytes really came
off the bus; `-2` (`BME68X_E_COM_FAIL`) means the ESP-IDF transfer itself
failed and the chip ID value is not bus evidence at all. The immediate re-read
is a single extra 1-byte frame, never retried, used only to tell a stable
wrong value from a value that changes between reads. Working units are
unaffected: nothing is logged differently on the success path.

### Reading the observed chip ID

The driver reports the value; the interpretation below is a checklist, not a
diagnosis.

| Observed | Plausible causes (verify physically) |
| --- | --- |
| `0xFF` | MISO never driven: no 3.3 V on VDD/VDDIO, no shared GND, CS never asserted (wrong pin / CSB not on GPIO27), breakout still strapped for I2C, SDO not on GPIO19 |
| `0x00` | MISO stuck low: short to GND, SDO/MOSI swapped at the header, sensor never selected |
| any other fixed value | Wrong part (confirm the marking), SDI/SDO swapped, or a bit-shifted read (`0xC2` = `0x61 << 1`) pointing at clock timing instead of wiring |
| value changes between the two reads | Nothing is driving a defined level: power/ground/strap problem before any signal-integrity work |
| `ESP_ERR_INVALID_RESPONSE` instead of `ESP_ERR_NOT_FOUND` | The ESP-IDF transfer failed (`BME68X_E_COM_FAIL`); GPIO conflicts or a bus not owned by this driver |

### Physical checks, in the order that costs least

1. **Get the actual breakout schematic/pinout.** Silkscreen names are not a
   schematic: confirm which pin is SDI (host→sensor), which is SDO
   (sensor→host) and which is CSB, and confirm the part is a **BME688**: a
   BME680 answers the same chip ID (`0x61`), so the log cannot tell the two
   apart and only the marking does.
2. **Power and ground.** Measure 3.3 V at the breakout's VDD **and** VDDIO pins
   (not 5 V), and confirm a shared GND between the breakout and the ESP32 with a
   continuity check - not just the same breadboard rail.
3. **SPI vs I2C strap.** The BME68x latches its interface at power-up from the
   CSB/SDO levels and most breakouts expose this as a jumper or solder bridge.
   Confirm the board is strapped for SPI and that CSB is really the pin wired to
   GPIO27; a strap still set to I2C leaves SDO silent, which reads as `0xFF`.
4. **Pin sense.** SDI (MOSI) must come from GPIO23 and SDO (MISO) must return on
   GPIO19. Swapping them still clocks the bus, so the symptom is `0x00`/`0xFF`,
   not a bus error. SCLK is GPIO18.
5. **SPI mode.** The driver is fixed at mode 0 (CPOL=0, CPHA=0) at 1 MHz, which
   the BME688 supports. Mode 3 is the other CPOL/CPHA combination the part
   accepts, so it is worth one instrumented experiment **after** steps 1-4 - do
   not switch it in firmware on a hunch, because a mode mismatch and an
   unpowered sensor produce the same log line.
6. **Instrument the bus.** A logic analyzer on CS (GPIO27), SCLK (GPIO18), MOSI
   (GPIO23) and MISO (GPIO19) at ≥ 10 MS/s answers in one capture: is CS low for
   the whole frame, are exactly 16 clocks issued (address + 1 data byte), does
   MISO idle high between frames and does the sensor drive it during the data
   byte. Without an analyzer: a multimeter shows CS idle high (bus released) and
   SDO idle high (pull-up present) with no transfers running, which already
   separates "sensor absent/silent" from "wrong data on a live bus".

### Information needed to close this out

The log alone cannot decide between wiring, strap, power and part problems, so a
report of this failure is only actionable with: the **breakout model and its
schematic/pinout**, the **observed chip ID byte and whether the re-read
matched**, the **`read=`/`write=` callback results**, and - if available - a
**logic-analyzer capture of the chip ID frame** or the multimeter readings from
step 6.

## API error codes

| Return | Meaning |
| --- | --- |
| `ESP_OK` | Measurement/config applied. Check gas flags. |
| `ESP_ERR_INVALID_ARG` | NULL argument, negative GPIO, heater value out of 200–400 °C / 1–4032 ms |
| `ESP_ERR_INVALID_STATE` | Handle not initialized, or `SPI2_HOST` already in use |
| `ESP_ERR_NO_MEM` | Context allocation failed |
| `ESP_ERR_TIMEOUT` | No new data within 25 ms after the conversion + heater window |
| `ESP_ERR_NOT_FOUND` | BME68x did not answer (`BME68X_E_DEV_NOT_FOUND`); the raw chip ID is logged, see troubleshooting above |
| `ESP_ERR_INVALID_RESPONSE` | SPI transfer failed (`BME68X_E_COM_FAIL`) |
| `ESP_ERR_INVALID_SIZE` | Unexpected register length (`BME68X_E_INVALID_LENGTH`) |
| `ESP_FAIL` | Other Bosch API error |

The sensor is driven from task context only (delay callback uses `vTaskDelay`)
and the adapter is not internally locked: use one handle from one task.

## Defaults

`BME68X_SPI_PINS_DEFAULT()` → GPIO 18/19/23/27.
`BME68X_SENSOR_CONFIG_DEFAULT()` → forced mode, OS 2X/4X/2X, IIR filter 3,
`BME68X_ODR_NONE`, heater 320 °C for 150 ms. This yields a ~171 ms conversion
(≈21 ms TPH + 150 ms heater).
