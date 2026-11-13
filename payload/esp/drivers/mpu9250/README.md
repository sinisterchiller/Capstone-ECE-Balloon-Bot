# MPU-9250 driver component (MPU-9250 + AK8963, I2C)

ESP-IDF component wrapping the LibDriver MPU-9250 driver plus a thin I2C adapter.
It exposes one nine-axis sample - accelerometer in g, gyroscope in dps,
magnetometer in uT - with an esp_timer timestamp. Polling only: no SPI, no
interrupt pin, no DMP, no FIFO.

## Vendor provenance

| Item | Value |
| --- | --- |
| Repository | https://github.com/libdriver/mpu9250 |
| Pinned commit | `06cc13e8d6f69e00e7a95f31e5794ce72d77e145` (tag `v1.0.11`, 2026-10-24, "Merge pull request #27 from libdriver/dev") |
| Driver version | 1.0.0 (`@version` in `src/driver_mpu9250.h`, `DRIVER_VERSION` 1000 in `src/driver_mpu9250.c`) |
| License | MIT, see `LICENSE` |
| Imported on | 2026-11-13 |

The vendor files are unmodified. Verified byte-identical to the pinned upstream
commit:

| File | SHA-256 |
| --- | --- |
| `src/driver_mpu9250.c` | `f59f41f4d85b84a91a2461f0583d7d7c58a624b631b295d36b7e2fa8491ecbb6` |
| `src/driver_mpu9250.h` | `b090a7b7e8cfe105a915c84b5f6108ec06737e0dfdbc0cbe420b21cdcb674d60` |
| `src/driver_mpu9250_code.h` | `f1313ff757677a17e57c98cd1d59e7f9dca1368178778d8fb98c41b743d5db22` |
| `LICENSE` | `81753bf783b8e971fcbcac6e4008d58f32c44818f3678df9774414565cb7c465` |

Re-verify with:

```sh
B=https://raw.githubusercontent.com/libdriver/mpu9250/06cc13e8d6f69e00e7a95f31e5794ce72d77e145
for f in src/driver_mpu9250.c src/driver_mpu9250.h src/driver_mpu9250_code.h LICENSE; do
  curl -sSL "$B/$f" | shasum -a 256
done
```

## Files

| File | Owner | Purpose |
| --- | --- | --- |
| `src/driver_mpu9250.c`, `src/driver_mpu9250.h`, `src/driver_mpu9250_code.h`, `LICENSE` | LibDriver | Registers, AK8963 handling, scaling. Do not edit. |
| `mpu9250_esp.c` | Project | I2C transport (read/write/delay/debug callbacks), sensor setup, error translation |
| `mpu9250_esp.h` | Project | Public adapter interface (`init`, `read`, `deinit`) |
| `CMakeLists.txt` | Project | Component registration (`esp_driver_i2c`, `esp_timer`) |

The upstream sources live in `src/`, never in `main/`.

## Wiring and bus configuration

| MPU-9250 pin | ESP32-WROOM |
| --- | --- |
| SDA | GPIO21 |
| SCL | GPIO22 |
| AD0 | GND -> address `0x68` |
| NCS | 3V3 (selects I2C) |
| FSYNC | GND |
| VCC / GND | 3V3 / GND |
| EDA, ECL, INT | not connected |

`mpu9250_esp_init()` creates I2C master bus 0 (`I2C_NUM_0`) at 100 kHz with the
internal pull-ups enabled and adds two devices to it: the MPU-9250 at `0x68` and
the AK8963 at `0x0C`, which is only reachable once the MPU's I2C bypass
(`INT_PIN_CFG` bit 1) is enabled - the upstream `mpu9250_mag_init()` does that.
The component owns the bus: `i2c_new_master_bus()` fails with
`ESP_ERR_INVALID_STATE` if another driver already initialized that port, and
`mpu9250_esp_deinit()` releases the devices and the bus again.

## Initialization sequence

`mpu9250_esp_init()` runs, in order:

1. `mpu9250_set_interface(MPU9250_INTERFACE_IIC)`, `mpu9250_set_addr_pin(MPU9250_ADDRESS_AD0_LOW)`
2. `mpu9250_init()` - WHO_AM_I (`0x75`) must read `0x71`, then a soft reset
3. `mpu9250_set_sleep(false)` - the reset leaves the device asleep
4. `mpu9250_set_clock_source(PLL)`, `mpu9250_set_sample_rate_divider(19)` (50 Hz),
   `mpu9250_set_low_pass_filter(MPU9250_LOW_PASS_FILTER_3)`
5. `mpu9250_set_accelerometer_range(+-2 g)`, `mpu9250_set_gyroscope_range(+-2000 dps)`
6. `mpu9250_mag_init()` - bypass on, AK8963 WIA (`0x00`) must read `0x48`, reset,
   fuse-ROM sensitivity adjustment (`ASAX/Y/Z`) read
7. `mpu9250_mag_set_mode(CONTINUOUS2)` (100 Hz), `mpu9250_mag_set_bits(16)`

Steps 3-7 mirror the upstream basic example; FIFO, DMP, interrupt and
wake-on-motion configuration are deliberately not touched.

## Measurement contract

`mpu9250_esp_read()` calls `mpu9250_read()` with a length of one sample. In the
non-FIFO path the driver reads the 14 output registers from `ACCEL_XOUT_H` and
then the eight AK8963 registers from `ST1`, and returns:

| Field | Unit | Notes |
| --- | --- | --- |
| `accel_g[3]` | g | Scaled by the driver from `ACCEL_CONFIG` (+-2 g -> 16384 LSB/g) |
| `gyro_dps[3]` | dps | Scaled from `GYRO_CONFIG` (+-2000 dps -> 16.4 LSB/dps) |
| `mag_ut[3]` | uT | AK8963 16-bit output, 0.15 uT/LSB, sensitivity-adjusted with the fuse-ROM ASA |
| `timestamp_us` | us | `esp_timer_get_time()` immediately after the register reads |

The timestamp is taken after the transfer, so it is the time the sample became
available to the caller, not the time the sensor converted it. The mag values are
whatever the AK8963 held at the moment of the read: the upstream driver does not
poll `ST1` for a new-data flag, so at 50 Hz sampling against a 100 Hz
magnetometer the same conversion can be returned twice. That is upstream
behaviour, not an adapter decision.

No calibration, tilt compensation, fusion or hard/soft-iron correction is
applied - the values are the sensor's own output.

## Callback contract (why the adapter looks like this)

* `iic_read(addr, reg, buf, len)` / `iic_write(addr, reg, buf, len)`: `addr` is the
  8-bit write address, `0xD0` for the MPU-9250 (AD0 low) and `0x18` for the AK8963
  (`0x0C << 1`). The adapter maps it back to the matching ESP-IDF device handle and
  returns `1` for anything else. Reads are one `i2c_master_transmit_receive()`
  (register byte, repeated start, data); writes are one `i2c_master_transmit()`
  of `{reg, data...}` so a register address is never separated from its payload
  by a stop condition. Writes are capped at 64 payload bytes, which covers every
  polling path (the largest is the 16-byte DMP memory chunk); FIFO data is read,
  not written.
* `iic_init()` / `iic_deinit()`: the adapter already owns the bus, so `iic_init()`
  only reports that it exists. `iic_deinit()` removes the device handles and
  deletes the bus - the upstream driver calls it from `mpu9250_deinit()` **and**
  from several `mpu9250_init()` failure paths, so the adapter's cleanup is
  idempotent and never frees the same handle twice.
* `spi_init/deinit/read/write`: `mpu9250_init()` validates **every** linked
  pointer, SPI included, before it looks at the selected interface. This
  integration is I2C-only, so those hooks are linked to stubs that report failure
  if they are ever reached - they are a requirement of the upstream init, not SPI
  support.
* `receive_callback`: required to be non-NULL by `mpu9250_init()`; a no-op because
  no interrupt pin is wired and the driver is polled.
* `delay_ms`: `vTaskDelay()`, rounded up to at least one tick for non-zero delays.
  The sensor is always driven from task context.
* `debug_print`: the upstream driver reports failures through this hook; the
  adapter forwards each message to `ESP_LOGE` (tag `mpu9250_esp`) with the
  trailing newline trimmed.

The upstream callbacks are plain function pointers without a context argument, so
the adapter keeps the single active device in a file-static pointer. A second
`mpu9250_esp_init()` while one device is live returns `ESP_ERR_INVALID_STATE`.
The callbacks never test the "initialized" flag: `mpu9250_init()` and
`mpu9250_mag_init()` issue transfers before init completes, so the bus and device
handles are the only preconditions.

## API error codes

| Return | Meaning |
| --- | --- |
| `ESP_OK` | Initialized / sample read / powered down |
| `ESP_ERR_INVALID_ARG` | NULL argument, or an upstream "handle is NULL" status |
| `ESP_ERR_INVALID_STATE` | Already initialized, I2C port 0 in use, handle not initialized, or an upstream mode error (DMP/FIFO) |
| `ESP_ERR_NO_MEM` | Context allocation failed |
| `ESP_ERR_NOT_FOUND` | WHO_AM_I (`0x75`) did not return `0x71`, or AK8963 WIA did not return `0x48` |
| `ESP_ERR_NOT_SUPPORTED` | Upstream reported an unsupported interface |
| `ESP_ERR_INVALID_RESPONSE` | An I2C register read failed |
| `ESP_ERR_INVALID_SIZE` | Upstream rejected the requested sample length |
| `ESP_FAIL` | Bus/device setup, reset or register configuration failed |

Upstream status codes are per-function, so the adapter translates them per
operation (`mpu9250_init`, `mpu9250_mag_init`, configuration setters,
`mpu9250_read`) instead of exposing one shared table. The raw code is logged as
`rslt=<n>` next to the failing call.

On a failed `mpu9250_esp_init()` the devices, the bus and the context are
released. A failed `mpu9250_esp_deinit()` power-down write is logged as a warning
and the resources are released anyway.

## Out of scope

SPI transport, interrupt/data-ready handling, DMP, FIFO and sensor fusion. The
component is driven from one task and is not internally locked.

## Validation

* Compiles and links against ESP-IDF v5.5.2 for `esp32` as part of the project
  build (`idf_component_register` with `esp_driver_i2c` and `esp_timer`).
* The upstream init/read sequence above was replayed on the host against a mock
  MPU-9250 + AK8963 register file (WHO_AM_I, reset, sleep, PLL, SMPLRT_DIV, DLPF,
  full-scale ranges, I2C bypass, AK8963 mode/bits, output-register scaling,
  bus-failure and wrong-ID paths). All checks passed; the harness is not part of
  this component.
* Not yet exercised on hardware.
