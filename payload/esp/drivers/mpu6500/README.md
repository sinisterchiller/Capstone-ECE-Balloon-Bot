# MPU-6500 driver component (six-axis, I2C)

ESP-IDF component wrapping the LibDriver MPU-6500 driver plus a thin I2C adapter.
It exposes one six-axis sample - accelerometer in g and gyroscope in dps - with an
`esp_timer` timestamp. Polling only: no magnetometer, no SPI, no interrupt pin, no
DMP, no FIFO.

The MPU-6500 is a 6-axis part. There is no AK8963 die behind it, so nothing here
touches the magnetometer registers, an I2C bypass or a second I2C address. A board
sold as "MPU-9250/6500" may contain either chip; `WHO_AM_I` decides, and only
`0x70` is accepted.

## Vendor provenance

| Item | Value |
| --- | --- |
| Repository | https://github.com/libdriver/mpu6500 |
| Pinned commit | `9d1b7f425b05edd9621f710e40cb84d381216577` (tag `v1.0.12`, 2026-10-24, "Merge pull request #13 from libdriver/dev") |
| Driver version | 1.0.0 (`@version` in `src/driver_mpu6500.h`, `DRIVER_VERSION` 1000 in `src/driver_mpu6500.c`) |
| License | MIT, see `LICENSE` |
| Imported on | 2026-11-13 |

The vendor files are unmodified. Verified byte-identical to the pinned upstream
commit:

| File | SHA-256 |
| --- | --- |
| `src/driver_mpu6500.c` | `2178b2b43d27d9fe3535efd8865ad772678f1dba2ab643c04093962dfd1353dd` |
| `src/driver_mpu6500.h` | `8fecbf1eecbd87f476333272053d6ee86ea783c4a2271652ce849259b8d4a2f4` |
| `src/driver_mpu6500_code.h` | `507956c967a18c41b5910fe9109a198676789e65ba64930e1265216893629582` |
| `LICENSE` | `81753bf783b8e971fcbcac6e4008d58f32c44818f3678df9774414565cb7c465` |

Re-verify with:

```sh
B=https://raw.githubusercontent.com/libdriver/mpu6500/9d1b7f425b05edd9621f710e40cb84d381216577
for f in src/driver_mpu6500.c src/driver_mpu6500.h src/driver_mpu6500_code.h LICENSE; do
  curl -sSL "$B/$f" | shasum -a 256
done
```

## Files

| File | Owner | Purpose |
| --- | --- | --- |
| `src/driver_mpu6500.c`, `src/driver_mpu6500.h`, `src/driver_mpu6500_code.h`, `LICENSE` | LibDriver | Registers, scaling, optional DMP. Do not edit. |
| `mpu6500_esp.c` | Project | I2C transport (read/write/delay/debug callbacks), sensor setup, error translation |
| `mpu6500_esp.h` | Project | Public adapter interface (`init`, `read`, `deinit`) |
| `CMakeLists.txt` | Project | Component registration (`esp_driver_i2c`, `esp_timer`) |

The upstream sources live in `src/`, never in `main/`. `driver_mpu6500_code.h`
carries the DMP firmware image and is included by `driver_mpu6500.c`; no DMP
symbol is referenced by this component, so the linker garbage-collects the
3062-byte image and the DMP code out of the final image.

## Wiring and bus configuration

| MPU-6500 pin | ESP32-WROOM |
| --- | --- |
| SDA | GPIO21 |
| SCL | GPIO22 |
| AD0 | GND -> address `0x68` |
| NCS | 3V3 (selects I2C) |
| FSYNC | GND |
| VCC / GND | 3V3 / GND |
| EDA, ECL, INT | not connected |

`mpu6500_esp_init()` creates I2C master bus 0 (`I2C_NUM_0`) at 100 kHz with the
internal pull-ups enabled and adds a single device, the MPU-6500 at `0x68`. The
component owns the bus: `i2c_new_master_bus()` fails with
`ESP_ERR_INVALID_STATE` if another driver already initialized that port, and
`mpu6500_esp_deinit()` releases the device and the bus again.

## Initialization sequence

`mpu6500_esp_init()` runs, in order:

1. Preflight WHO_AM_I read through the I2C device handle (see below)
2. `mpu6500_set_interface(MPU6500_INTERFACE_IIC)`, `mpu6500_set_addr_pin(MPU6500_ADDRESS_AD0_LOW)`
3. `mpu6500_init()` - WHO_AM_I (`0x75`) must read `0x70`, then a soft reset that
   is polled for up to ~1 s
4. `mpu6500_set_sleep(false)` - the reset leaves the device asleep
5. `mpu6500_set_clock_source(PLL)`, `mpu6500_set_sample_rate_divider(19)` (50 Hz),
   `mpu6500_set_low_pass_filter(MPU6500_LOW_PASS_FILTER_3)`
6. `mpu6500_set_accelerometer_range(+-2 g)`, `mpu6500_set_gyroscope_range(+-2000 dps)`

Steps 4-6 mirror the upstream basic example defaults. FIFO, DMP, interrupt,
wake-on-motion and magnetometer configuration are deliberately not touched, and
no second I2C device is added.

The preflight read is diagnostic only. `mpu6500_init()` reports a wrong ID and a
failed transfer as the same status 5 with the message `mpu6500: id is invalid`, so
the adapter reads `0x75` once through the same `i2c_master_device` handle the
callbacks use and logs the outcome before the upstream check runs:

| Log line | Meaning | Returned |
| --- | --- | --- |
| `WHO_AM_I (0x75) at 0x68 = 0x70` | Sensor answered with the MPU-6500 ID | continues into `mpu6500_init()` |
| `WHO_AM_I (0x75) at 0x68 returned 0xNN, expected 0x70` | Something else answered at that address (an MPU-9250 returns `0x71`); check what is on the bus | `ESP_ERR_NOT_FOUND` |
| `WHO_AM_I (0x75) read from 0x68 failed: <esp_err>` | I2C transfer failed; investigate transport error | `ESP_ERR_INVALID_RESPONSE` |

No other ID is accepted and the upstream validation is unchanged: the preflight
can only fail `mpu6500_esp_init()` earlier with a more specific error, never make
it succeed where `mpu6500_init()` would have rejected the chip. It runs before the
upstream driver is linked to the bus, so a failure cannot leave the driver
half-initialized: it takes the normal `fail:` path, which releases the device, the
bus and the context.

## Measurement contract

`mpu6500_esp_read()` calls `mpu6500_read()` with a length of one sample. With FIFO
disabled (`USER_CTRL` bit 6 clear, which the adapter never sets) the driver reads
the 14 output registers from `ACCEL_XOUT_H` and returns:

| Field | Unit | Notes |
| --- | --- | --- |
| `accel_g[3]` | g | Scaled by the driver from `ACCEL_CONFIG` (+-2 g -> 16384 LSB/g) |
| `gyro_dps[3]` | dps | Scaled from `GYRO_CONFIG` (+-2000 dps -> 16.4 LSB/dps) |
| `timestamp_us` | us | `esp_timer_get_time()` immediately after the register reads |

The timestamp is taken after the transfer, so it is the time the sample became
available to the caller, not the time the sensor converted it. The temperature
registers inside that same 14-byte burst are discarded: the driver's
`mpu6500_read()` does not expose them.

No calibration, offset, fusion or self-test is applied - the values are the
sensor's own output scaled by the configured full-scale range.

## Callback contract (why the adapter looks like this)

* `iic_read(addr, reg, buf, len)` / `iic_write(addr, reg, buf, len)`: `addr` is the
  8-bit write address from `mpu6500_set_addr_pin()` (`0xD0` for AD0 low). The
  adapter maps it back to the single ESP-IDF device handle and returns `1` for
  anything else. Reads are one `i2c_master_transmit_receive()` (register byte,
  repeated start, data); writes are one `i2c_master_transmit()` of
  `{reg, data...}` so a register address is never separated from its payload by a
  stop condition. Writes are capped at 16 payload bytes, which covers every
  polling path (they write a single register byte); DMP memory loads, which write
  more, are not used.
* `iic_init()` / `iic_deinit()`: the adapter already owns the bus, so `iic_init()`
  only reports that it exists. `iic_deinit()` removes the device handle and
  deletes the bus - the upstream driver calls it from `mpu6500_deinit()` **and**
  from several `mpu6500_init()` failure paths, so the adapter's cleanup is
  idempotent and never frees the same handle twice.
* `spi_init/deinit/read/write`: `mpu6500_init()` validates **every** linked
  pointer, SPI included, before it looks at the selected interface. This
  integration is I2C-only, so those hooks are linked to stubs that report failure
  if they are ever reached - they are a requirement of the upstream init, not SPI
  support.
* `receive_callback`: required to be non-NULL by `mpu6500_init()`; a no-op because
  no interrupt pin is wired and the driver is polled.
* `delay_ms`: `vTaskDelay()`, rounded up to at least one tick for non-zero delays.
  The sensor is always driven from task context.
* `debug_print`: the upstream driver reports failures through this hook; the
  adapter forwards each message to `ESP_LOGE` (tag `mpu6500_esp`) with the
  trailing newline trimmed.

The upstream callbacks are plain function pointers without a context argument, so
the adapter keeps the single active device in a file-static pointer. A second
`mpu6500_esp_init()` while one device is live returns `ESP_ERR_INVALID_STATE`.
The callbacks never test the "initialized" flag: `mpu6500_init()` issues transfers
before init completes, so the bus and device handles are the only preconditions.

## API error codes

| Return | Meaning |
| --- | --- |
| `ESP_OK` | Initialized / sample read / powered down |
| `ESP_ERR_INVALID_ARG` | NULL argument, or an upstream "handle is NULL" status |
| `ESP_ERR_INVALID_STATE` | Already initialized, I2C port 0 in use, handle not initialized, or an upstream mode error (DMP/FIFO) |
| `ESP_ERR_NO_MEM` | Context allocation failed |
| `ESP_ERR_NOT_FOUND` | WHO_AM_I (`0x75`) did not return `0x70` |
| `ESP_ERR_INVALID_RESPONSE` | An I2C register read failed (including the WHO_AM_I preflight) |
| `ESP_ERR_INVALID_SIZE` | Upstream rejected the requested sample length |
| `ESP_FAIL` | Bus/device setup, reset or register configuration failed |

Upstream status codes are per-function, so the adapter translates them per
operation (`mpu6500_init`, configuration setters, `mpu6500_read`) instead of
exposing one shared table. The raw code is logged as `rslt=<n>` next to the
failing call.

On a failed `mpu6500_esp_init()` the device, the bus and the context are released.
A failed `mpu6500_esp_deinit()` power-down write is logged as a warning and the
resources are released anyway.

## Out of scope

Magnetometer/AK8963, SPI transport, interrupt/data-ready handling, FIFO, DMP,
self-test, calibration and sensor fusion. The component is driven from one task
and is not internally locked.

## Validation

* The vendor files are byte-identical to the pinned upstream commit (SHA-256
  table above, re-downloaded from `raw.githubusercontent.com`).
* Compiles and links against ESP-IDF v5.5.2 for `esp32` as part of a project build
  (`idf_component_register` with `esp_driver_i2c` and `esp_timer`), with no
  compiler warnings from either translation unit.
* The init/read sequence above was replayed on the host against a mock MPU-6500
  register file (`WHO_AM_I` 0x70 accepted, 0x71 rejected, AD0-high address
  rejected, reset polled, the six settings read back through the getters, 14-byte
  burst scaling at 16384 LSB/g and 16.4 LSB/dps, dead-bus and failed-read paths).
  The harness is scratch-only and is not part of this component.
* Not yet exercised on hardware.
