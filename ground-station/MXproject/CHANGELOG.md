# MXproject MPU/GPS/LoRa Integration — Changelog

## Overview

This changelog documents the complete MPU (MPU-9250/MPU-6500) integration with the existing GPS/LoRa telemetry chain in the MXproject STM32WL dual-core firmware. The work adds a 100 Hz IMU polling loop on CM4 (CPU1), publishes orientation snapshots through SRAM1 shared memory using a seqlock protocol, encodes a combined GPS+MPU v3 telemetry packet (26 bytes) on CM0+ (CPU2), and transmits it over LoRa with ACK retransmission.

**Firmware version:** v3 (wire protocol version 3)
**Branch:** `feature/ground-station`
**Target hardware:** STM32WL55JC (dual-core: CM4 + CM0+), MPU-9250 or MPU-6500 at I2C2 (PA11/PA12), SAM-M10Q GNSS on USART1, LoRa on SubGHz radio.

---

## 1. I2C2 Bring-Up and Electrical Diagnostics

### 1.1 Peripheral Configuration
- **I2C2** on PA11 (SDA) / PA12 (SCL), AF4, open-drain with internal pull-ups.
- Clock source: `RCC_I2C2CLKSOURCE_SYSCLK` (system clock).
- Timing register: `0x00201313` (nominal 100 kHz target at 4 MHz PCLK). *Note: actual SCL frequency depends on bus rise/fall times and has not been independently verified with a timing calculator or scope.*
- Analog filter enabled, digital filter disabled (0).
- Interrupts explicitly **disabled** in `HAL_I2C_MspInit` — only blocking I2C transactions are used; enabling IRQs without handlers would lock the core in the default handler.

### 1.2 Pre-Probe Diagnostic (Logged Before First MPU Probe)
Captured once at boot, before CPU2 is released, and enqueued to the VCP ring (with bounded retries if the ring is full before CPU2 drains it):
```
I2C GPIO: MODE=0xA AF=0x44 OD=0x3
I2C HW: CR1=0x... TIMING=0x00201313
I2C: CLK=4000000 SDA=1 SCL=1
```
- `CLK=4000000` = I2C2 kernel clock (4 MHz) from `HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_I2C2)`.
- `SDA=1 SCL=1` = live GPIO IDR line levels (both high = bus idle, no stuck-low condition).
- This diagnostic proved the I2C peripheral is clocked, pins are in AF open-drain, and the bus is electrically idle before any sensor transaction.

### 1.3 Device Probe at Both Addresses
`MPU_Probe()` iterates **0x68 (AD0 low)** then **0x69 (AD0 high)**:
- Performs `HAL_I2C_IsDeviceReady` (3 trials × 10 ms).
- Captures HAL status, HAL error code, and `I2C2->ISR` atomically per address.
- Reads `WHO_AM_I` (0x75) on success.
- **Hardware evidence:** MPU-6500 detected at 0x68 with `WHO_AM_I=0x70`. Log line:
  ```
  MPU probe 0x68: status=0 err=0x00000000 ISR=0x00000000 WHO_AM_I=0x70
  MPU detected: MPU-6500 (WHO_AM_I=0x70)
  ```
- MPU-9250 (WHO_AM_I=0x71) supported but **not physically tested** on this board.
- Unknown WHO_AM_I values are logged and the probe continues to the next address.

### 1.4 Diagnostic WHO_AM_I Read Helper
`I2C2_ReadChipIdDirectDiagnostic(addr, &id)` provides a single 100 ms timeout read with full error/ISR capture, used for bring-up visibility independent of the driver probe sequence.

---

## 2. MPU Driver (CM4/Core/Src/mpu.c/h)

### 2.1 Supported Devices and Model Enumeration
| Device | WHO_AM_I | Driver enum | Wire model ID (telemetry) |
|--------|----------|-------------|---------------------------|
| MPU-9250 | 0x71 | `MPU_MODEL_9250 = 1` | `GNSS_VCP_MPU_MODEL_9250 = 2` |
| MPU-6500 | 0x70 | `MPU_MODEL_6500 = 2` | `GNSS_VCP_MPU_MODEL_6500 = 1` |
| Unknown | — | `MPU_MODEL_UNKNOWN = 0` | `GNSS_VCP_MPU_MODEL_UNKNOWN = 0` |

The driver and wire numbering differ intentionally; `MPU_ModelToWireId()` maps explicitly so the ground station never confuses the two sensors.

### 2.2 Initialization Sequence (Accel/Gyro)
1. Reset (`PWR_MGMT_1 = 0x80`), wait 100 ms.
2. Wake + PLL with X-gyro reference (`CLKSEL = 0x01`), wait 10 ms.
3. Disable I2C master (bypass mode for AK8963).
4. Sample rate divider = 9 → 1 kHz / (1+9) = **100 Hz**.
5. DLPF: gyro 41 Hz, accel 41 Hz (`CONFIG = 0x03`, `ACCEL_CONFIG2 = 0x03`).
6. Gyro range ±250 °/s (`GYRO_CONFIG = 0x00`).
7. Accel range ±2g (`ACCEL_CONFIG = 0x00`).
8. Enable all axes (`PWR_MGMT_2 = 0x00`).
9. Enable I2C bypass (`INT_PIN_CFG = 0x02`).
10. **Read back all 8 key registers**; any mismatch → `HAL_ERROR`.

### 2.3 Magnetometer Initialization (MPU-9250 Only, AK8963)
Datasheet-correct sequence:
1. Ensure MPU I2C master disabled.
2. Read `WIA` (0x00), expect 0x48.
3. Power-down (`CNTL1 = 0x00`), wait 10 ms.
4. Fuse ROM access (`CNTL1 = 0x0F`), wait 10 ms.
5. Read ASA[XYZ] (0x10–0x12) → store **immutable factory sensitivity** multipliers `(ASA + 128) / 256`.
6. Power-down again, wait 10 ms.
7. Configure 16-bit continuous 100 Hz (`CNTL1 = 0x16` per task1.md), wait 10 ms.
8. Verify `CNTL1 = 0x16`.

**Failure policy:** Magnetometer init failure clears `mag_available` but **does not error out** — accel/gyro remain fully operational. Heading is reported unavailable.

### 2.4 100 Hz Non-Blocking Poll (`MPU_Poll`)
- Called from CM4 main loop (~100 Hz, 10 ms interval).
- **14-byte burst read** from 0x3B (accel X/Y/Z, temp, gyro X/Y/Z).
- On success: scales raw → physical units (g, °/s, °C), runs calibration state machine, updates orientation.
- On MPU-9250 with `mag_available`: reads AK8963 `ST1`; if `DRDY`, reads 7 bytes (HXL–HZH + ST2), applies factory sensitivity (0.15 µT/LSB), checks `HOFL` overflow.
- Returns `HAL_OK` if accel/gyro read succeeded (mag failure tracked separately), `HAL_ERROR` on accel/gyro failure, `HAL_BUSY` if not yet time for next poll.
- `consec_failures` counter increments on accel/gyro I2C errors; driver never resets the bus automatically.

### 2.5 Orientation Calculation
**Roll/Pitch (both models):** Complementary filter (α = 0.98 gyro weight).
- Accel-derived: `roll = atan2(ay, az)`, `pitch = atan2(-ax, √(ay²+az²))`.
- Gyro-integrated with bias correction (bias calibrated at startup when stationary).
- Roll normalized to [−180, +180]; pitch clamped to [−90, +90].

**Heading (MPU-9250 only):** Tilt-compensated magnetic heading.
- **Prerequisites for `heading_valid = true` (ALL must hold):**
  1. `model == MPU_MODEL_9250`
  2. `mag_available == true` (AK8963 init succeeded)
  3. `mag_scaled.valid == true` (current sample DRDY, no HOFL)
  4. `mag_calib.valid == true` (hard-iron offsets calibrated with sufficient range)
  5. `mag_align.known == true` (axis alignment matrix physically calibrated)
  6. `mag_soft_iron.valid == true` (soft-iron scale calibrated)
- If any prerequisite fails, `heading_valid = false`, `heading_deg = 0`.
- Default alignment matrix assumes standard MPU-9250 chip-frame mapping (AK8963 +X → MPU −Y, +Y → MPU +X, +Z → MPU −Z):
  ```
  [  0,  1,  0]
  [ -1,  0,  0]
  [  0,  0, -1]
  ```
- **Caveat:** `mag_align.known` defaults to `true` for MPU-9250 with the standard matrix, but **this has not been physically validated** against a known heading reference. For a non-standard board mounting, call `MPU_SetMagAlignment()` after init. Soft-iron defaults to identity with `valid = true` (no correction assumed).
- Magnetometer hard-iron calibration requires **physical movement** covering >10 µT range on all three axes (500 samples). Insufficient range → calibration restarts, `mag_calib.valid` stays false → heading never becomes valid.

### 2.6 Startup Calibration
1. **Gyro bias (200 samples):** Only accumulates when stationary (accel magnitude ~1g ±5%, gyro magnitude <2 °/s). Movement resets accumulation.
2. **Mag hard-iron (500 samples, MPU-9250 only):** Tracks min/max per axis; requires >10 µT range on each axis. Insufficient range → reports failure and restarts.
3. Calibration state machine: `1 = gyro` → `2 = mag (9250 only)` → `3 = done`.

### 2.7 Bring-Up Retry Policy (Main Loop)
- `MPU_BRINGUP_MAX_ATTEMPTS = 5`, `MPU_BRINGUP_RETRY_MS = 1000`.
- Boot: wait `MPU_DELAY_POR_MS (100 ms)`, then `MPU_BringUpAttempt()`.
- On failure: handle stays uninitialized, main loop retries every 1 s until budget spent.
- On success: publishes model detection to VCP (`MPU ok: MPU-6500 initialized`).
- After budget exhausted: logs `MPU fail: bring-up retries exhausted`, stops touching I2C bus for the rest of the flight.
- While uninitialized: publishes invalid MPU status once per second so CPU2 never sees stale data from a previous run.

### 2.8 VCP Trace Logging (Rate-Limited)
- **Poll rate:** 100 Hz. **Log rate:** 1 Hz (every 100 polls).
- Line 1 (accel/gyro/roll/pitch): `MPU A=...mg G=...mdps R=...cdeg P=...cdeg` (integer fixed-point, no `%f`).
- Line 2 (heading/model/sequence): `MPU H=...cdeg M=... S=...` or `MPU H=UNAVAILABLE M=... S=...` for MPU-6500.
- Trace state machine (`MPU_TRACE_UNREPORTED/VALID/FAILED`) emits one line on valid→failed or failed→valid transitions, not on every poll.

---

## 3. SRAM1 Shared Memory — Seqlock / Freshness Protocol

### 3.1 Mailbox Layout (Common/GnssVcp/gnss_vcp_mailbox.h)
```
Offset 0–519:   GnssVcpMailbox (520 B) — NMEA sentence ring (CPU1→CPU2)
Offset 520–547: GnssVcpGpsStatus (28 B) — GPS snapshot
Offset 548–567: GnssVcpMpuStatus (20 B) — MPU orientation snapshot (NEW, replaces former BNO055 slot)
Total:          568 B < 1 KB reserved region (GNSS_VCP_MAILBOX_RESERVED)
```
Compile-time `_Static_assert` checks in both CM4 (`gnss_vcp.c`) and CM0+ (`mpu_status.c`) enforce layout matching.

### 3.2 MPU Status Structure (20 bytes)
```c
typedef struct {
  volatile uint32_t seq;                 // seqlock: odd = writing, even = stable
  volatile uint32_t update_timestamp_ms; // CPU1 HAL_GetTick() when data read (diagnostic only)
  volatile uint8_t  valid;               // 1 = roll/pitch valid (accel/gyro derived)
  volatile uint8_t  heading_valid;       // 1 = heading valid (MPU-9250 mag + calib)
  volatile uint8_t  model;               // GNSS_VCP_MPU_MODEL_*
  volatile uint8_t  initialized;         // GNSS_VCP_MPU_STATUS_INIT_MARK (0xA6) once set up
  volatile int16_t  heading;             // degrees * 16, 0 when heading_valid=0
  volatile int16_t  roll;                // degrees * 16
  volatile int16_t  pitch;               // degrees * 16
  volatile uint8_t  _reserved[2];
} GnssVcpMpuStatus;
```
**Critical design:** `valid` covers accel/gyro orientation only. `heading_valid` is a separate flag — **never derived from `valid`**. An MPU-6500 publishes `valid=1, heading_valid=0, heading=0`.

### 3.3 CPU1 Publication (Seqlock Write)
`GNSS_VCP_PublishMpuStatus()`:
1. `seq += 1` (make odd) → `BARRIER()`.
2. Write all fields + `initialized = 0xA6`.
3. `BARRIER()` → `seq += 1` (make even).

### 3.4 CPU2 Consumption (Seqlock Read + Freshness Watch)
`MPU_STATUS_ReadLatest()` (CM0PLUS/Core/Src/mpu_status.c):
- Retries coherent seqlock read up to 3 times.
- Rejects if `initialized != 0xA6` (never initialised or CPU1 restarted).
- **First observation gate:** The first coherent publication after CPU2 (re)start is **only remembered, never presented** — its age is unbounded because CPU1 may have published it arbitrarily long before.
- **Freshness on CPU2's clock:** Age = `HAL_GetTick() - observed_ticks` (both CPU2 RTC ticks, 1024 Hz). Max age = `GNSS_VCP_MPU_MAX_DATA_AGE_MS = 500 ms` (512 ticks).
- A reading is presentable only if:
  - The publication was seen to **replace an earlier one within one age window** (`change_seen = true`), so `observed_ticks` actually bounds its age, AND
  - The elapsed ticks since that observation ≤ 512.
- If CPU1 stops publishing, the last reading expires after 500 ms on CPU2's clock and stays expired until a new publication arrives.
- `MPU_STATUS_ObserveFreshness()` called from a ~100 ms periodic timer (102 ticks) updates the watch state between 10 s telemetry reads, ensuring `change_seen` becomes true when CPU1 publishes within the window.

---

## 4. Wire Protocol v3 — Combined GPS + MPU Telemetry

### 4.1 Packet Layout (26 bytes, little-endian)
| Byte | Field | Description |
|------|-------|-------------|
| 0 | version | 3 |
| 1 | type | 0x01 (GPS+MPU status) |
| 2–3 | sequence | uint16, increments per TX |
| 4 | fix_valid | 1 = valid GPS fix |
| 5–8 | latitude | int32 microdegrees (×1e6), 0 if no fix |
| 9–12 | longitude | int32 microdegrees, 0 if no fix |
| 13–16 | altitude | int32 centimetres, 0 if no fix |
| 17 | satellites | uint8, 0 if no fix |
| 18 | imu_flags | bit0=orient_valid, bit1=heading_valid |
| 19–20 | heading | int16 degrees × 16 (0..5760), 0 if heading invalid |
| 21–22 | roll | int16 degrees × 16 (−2880..+2880) |
| 23–24 | pitch | int16 degrees × 16 (−1440..+1440) |
| 25 | imu_model | 0=unknown, 1=MPU-6500, 2=MPU-9250 |

**Strict length check:** Decode rejects any buffer ≠ 26 bytes (no trailing bytes accepted).
**ACK packet (4 bytes):** version=3, type=0x02, sequence (echoed). Unchanged from v2.

### 4.2 Encoding (`Telemetry_EncodeCombinedStatus`)
- Validates GPS bounds when `fix_valid`: lat ±90°, lon ±180°, alt −1000..+50000 m, sats ≤ 255.
- Validates MPU bounds when `orient_valid`:
  - Model must be 1 or 2.
  - `heading_valid` only allowed for model=2 (MPU-9250); MPU-6500 never sets it.
  - When `heading_valid=0`, heading must be 0.
  - Roll ±180°×16, Pitch ±90°×16.
- When `orient_valid=0`: all MPU fields zero, model=0.
- GPS and MPU fail independently; the other's data continues.

### 4.3 Decoding (`Telemetry_DecodeCombinedStatus`)
- Strict version=3, type=0x01, length=26.
- Validates all field bounds on decode (mirrors encoder).
- Rejects reserved flag bits, `heading_valid` without `orient_valid`, non-zero heading when `heading_valid=0`, non-zero MPU fields when `orient_valid=0`.
- On any failure, output structures unmodified.

### 4.4 Legacy Compatibility
- v1 (GPS only, 18 bytes) and v2 (BNO055, 26 bytes) encode/decode retained as deprecated functions.
- ACK version bumped to 3 to match telemetry version.

---

## 5. CM0+ Telemetry Application (SubGHz_Phy/App/subghz_phy_app.c)

### 5.1 Packet Build (`Telemetry_BuildAndTransmit`)
- Reads GPS status via `GNSS_STATUS_ReadLatest()`.
- Reads MPU status via `MPU_STATUS_ReadLatest()` (seqlock + freshness).
- Encodes v3 combined packet.
- Logs GPS fix + MPU status (model, validity, heading/roll/pitch in human-readable degrees via integer fixed-point helpers).

### 5.2 Transmission & ACK Retransmit
- **Telemetry interval:** 10 s (`TELEMETRY_TX_INTERVAL_MS`).
- **ACK timeout:** 2 s (`ACK_TIMEOUT_MS`), one-shot timer.
- **Retransmit:** Up to 3 retries (immediate on timeout). State machine: `WAIT_INTERVAL → TX → WAIT_ACK → (TX on timeout) → ...`.
- Duplicate ACK detection via `last_rx_sequence`.

### 5.3 Ground Station RX (`GroundRx_Process`)
- Decodes v3 packet via `Telemetry_DecodeCombinedStatus()`.
- Logs GPS (microdegrees, metres) + MPU (model, heading/roll/pitch in degrees).
- Sends ACK for every valid packet (including duplicates).

### 5.4 MPU Freshness Observation Timer
- Periodic timer `MPU_FRESHNESS_INTERVAL_MS = 100 ms` (102 ticks at 1024 Hz).
- Callback `OnMpuFreshnessTimer` → `MPU_STATUS_ObserveFreshness()`.
- Runs in CPU2 application context (timer callback), not interrupt.
- Ensures the snapshot watch observes publications within the 500 ms window so `change_seen` becomes true before the 10 s telemetry read.

---

## 6. Host Tests (Pure C, No Hardware)

All tests compile with `-std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined` and run on the development machine.

| Test File | Scope | Key Validations |
|-----------|-------|-----------------|
| `CM4/tests/mpu_test.c` | MPU driver logic (scaling, complementary filter, calibration, mag sensitivity, orientation, roll normalization, pitch clamping, stationary detection, model strings) | 15 test functions, all pass |
| `Common/Telemetry/telemetry_test.c` | v1 (18 B) and v3 (26 B) encode/decode, ACK (4 B), bounds, extra bytes, version/type mismatch, signed MPU fields, MPU-6500 heading exclusion | 27+ test functions, all pass |
| `Common/Telemetry/telemetry_mpu_status_test.c` | CPU2 snapshot consumer: first-observation gate, replacement within window, heading_valid not conflated, 500 ms expiration on CPU2 clock, published invalid, uninitialised region | 6 test functions, all pass |

**Build commands verified:**
```sh
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
   -I Common/Telemetry -I CM4/Core/Inc -DMPU_HOST_TEST \
   CM4/tests/mpu_test.c -o /tmp/mpu_test -lm && /tmp/mpu_test

cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
   -I Common/Telemetry -I CM4/Core/Inc \
   Common/Telemetry/telemetry.c Common/Telemetry/telemetry_test.c \
   -o /tmp/telemetry_test && /tmp/telemetry_test

cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
   -DGNSS_VCP_BARRIER_HOST -I Common/GnssVcp -I CM0PLUS/Core/Inc \
   CM0PLUS/Core/Src/mpu_status.c Common/Telemetry/telemetry_mpu_status_test.c \
   -o /tmp/mpu_status_test && /tmp/mpu_status_test
```

---

## 7. Real Hardware Validation Evidence

### 7.1 Proven on Hardware (MPU-6500 at 0x68)
- I2C2 peripheral clock: 4 MHz (`CLK=4000000`).
- GPIO AF/OD config: `MODE=0xA AF=0x44 OD=0x3` (PA11/PA12 = AF4 open-drain).
- Bus idle: `SDA=1 SCL=1`.
- Device ready at 0x68: `HAL_I2C_IsDeviceReady` status=0, err=0, ISR=0.
- `WHO_AM_I=0x70` → MPU-6500 identified.
- Accel/gyro init: all 8 register readbacks match.
- 100 Hz burst reads: `MPU_Poll` returns `HAL_OK`, `valid=1`.
- Orientation: roll/pitch published, `heading_valid=0`, `heading=0`.
- Telemetry v3 packets: encoded, transmitted, ACKed, decoded on ground.
- VCP trace: `I2C: CLK=4000000 SDA=1 SCL=1`, `MPU ok: MPU-6500 initialized`, `MPU ok: readings valid`, periodic 1 Hz log lines with integer mg/mdps/cdeg.

### 7.2 NOT Physically Tested / Unvalidated
| Item | Status |
|------|--------|
| MPU-9250 (WHO_AM_I=0x71) detection | Code path present, no hardware |
| AK8963 magnetometer init (`CNTL1=0x16`, ASA read) | Code path present, no hardware |
| Magnetometer hard-iron calibration (requires movement) | Logic tested in host tests, not on hardware |
| Magnetometer soft-iron calibration | Defaults to identity, not calibrated |
| Axis alignment matrix (`mag_align`) physical validation | Defaults to datasheet standard, not verified on board |
| Tilt-compensated heading accuracy | Not measured |
| I2C2 actual SCL frequency (scope/timing calculator) | Nominal 100 kHz target only |
| 9250 mag data ready / overflow handling (`ST1` DRDY, `ST2` HOFL) | Code path present, not exercised |
| MPU-9250 full orientation (roll/pitch/heading) telemetry end-to-end | Not tested |
| LoRa range / packet loss statistics | Not in scope of this integration |

---

## 8. Build Artifacts (Not Committed)

The following are generated by the CMake/Ninja build and **must not be committed**:
- `ground-station/MXproject/build/` — entire build tree (payload/ground, CM4/CM0+, Debug/)
- `*.elf`, `*.bin`, `*.hex`, `*.map` — flash images and maps
- `compile_commands.json`, `build.ninja`, `CMakeCache.txt`, `.ninja_*`
- `CMakeFiles/` directories

Only source files, CMakeLists, and this changelog are versioned.

---

## 9. Files Changed (Task-Related, Staged for Commit)

### CM4 (CPU1 — Payload)
- `CM4/Core/Inc/gnss_vcp.h` — Added `GNSS_VCP_PostText()` for diagnostic lines.
- `CM4/Core/Src/gnss_vcp.c` — Implemented `GNSS_VCP_PostText()`, compile-time layout asserts, disabled raw NMEA debug.
- `CM4/Core/Inc/stm32wlxx_hal_conf.h` — Enabled `HAL_I2C_MODULE_ENABLED`, added `hal_i2c_ex.h`.
- `CM4/Core/Src/main.c` — MPU bring-up, I2C2 init, pre-probe diagnostic, 100 Hz poll loop, VCP logging, shared memory publish.
- `CM4/Core/Src/stm32wlxx_hal_msp.c` — `HAL_I2C_MspInit`/`MspDeInit` for I2C2 (PA11/PA12, AF4, OD, pull-up, no IRQ).
- `CM4/Core/Inc/i2c.h` — **New.** I2C2 diagnostic probe, WHO_AM_I read helpers, timing register docs.
- `CM4/Core/Src/i2c.c` — **New.** `MX_I2C2_Init()`, `I2C2_ProbeAddrDiagnostic()`, `I2C2_ReadChipIdAtAddr()`, `I2C2_ReadChipIdDirectDiagnostic()`.
- `CM4/Core/Inc/mpu.h` — **New.** Full MPU-9250/6500 driver API, register map, scaling, orientation, calibration types.
- `CM4/Core/Src/mpu.c` — **New.** Driver implementation (probe, init, mag init, 100 Hz poll, orientation, calibration, bring-up retry).
- `CM4/CMakeLists.txt` — Added `i2c.c`, `mpu.c`, I2C HAL driver sources.
- `CM4/tests/mpu_test.c` — **New.** Host logic tests for MPU driver.

### Common (Shared CM4/CM0+)
- `Common/GnssVcp/gnss_vcp_mailbox.h` — MPU status structure, offsets, seqlock publish/read inlines, model IDs, max age.
- `Common/Telemetry/telemetry.h` — v3 protocol constants, `TelemetryMpuStatus`, combined encode/declare.
- `Common/Telemetry/telemetry.c` — v3 encode/decode with full bounds validation, legacy v1 retained.
- `Common/Telemetry/telemetry_test.c` — Extended for v3 + v1 + ACK + MPU bounds.

### CM0+ (CPU2 — Payload Radio / Ground RX)
- `CM0PLUS/SubGHz_Phy/App/subghz_phy_app.c` — v3 packet build, MPU freshness timer, ground RX decode/log, ACK retransmit.
- `CM0PLUS/subghz_phy.cmake` — Added `mpu_status.c` to CM0+ sources.
- `CM0PLUS/Core/Inc/mpu_status.h` — **New.** CPU2 consumer API, `MpuTelemetryStatus`, `MpuSnapshotWatch`.
- `CM0PLUS/Core/Src/mpu_status.c` — **New.** Seqlock read + freshness watch (CPU2 clock), host-testable variants.
- `Common/Telemetry/telemetry_mpu_status_test.c` — **New.** Host tests for CPU2 snapshot consumer.

### Documentation
- `CHANGELOG.md` — **New.** This file.

---

## 10. Files Explicitly Excluded from This Commit (Pre-existing / Unrelated / Untested)

| Path | Reason |
|------|--------|
| `ground-station/stmLora-trial/` | Separate trial directory, not part of MXproject integration |
| `ground-station/.vscode/` | Editor config, not source |
| `ground-station/MXproject/repomix-output.xml` | Generated analysis artifact |
| `ground-station/MXproject/tasks/task1.md` | Task planning document |
| `ground-station/MXproject/acceptance-report*.json` | CI artifacts |
| `CM0PLUS/Core/Inc/bno055_status.h`, `bno055_status.c` | Obsolete BNO055 consumer (untracked) |
| `CM4/Core/Inc/bno055.h`, `CM4/Core/Src/bno055.c` | Obsolete BNO055 driver (untracked) |
| `Common/Telemetry/telemetry_bno055_status.h`, `telemetry_bno055_status_test.c` | Obsolete BNO055 telemetry (untracked) |
| `Common/Telemetry/telemetry_mpu_align_test.c`, `telemetry_mpu_i2c_test.c` | Incomplete/unvalidated test stubs (untracked) |
| `Drivers/STM32WLxx_HAL_Driver/Inc/Src/stm32wlxx_hal_i2c*` | HAL driver updates (vendor code, tracked but not task-authored) |
| `CM0PLUS/README-PingPong.md` | Toolchain notes, unrelated to MPU integration |
| `CM4/Core/Inc/gnss_vcp.h` (raw NMEA debug flag change) | Included above as task-related |

---

## 11. Residual Risks & Open Items

1. **MPU-9250 / AK8963 never physically tested.** The driver code paths exist and compile, but no hardware validation has been done. Magnetometer init, ASA reading, 100 Hz continuous mode, DRDY/HOFL handling, and tilt-compensated heading are untested on real silicon.
2. **Axis alignment (`mag_align`) not validated.** The default matrix assumes standard MPU-9250 chip-frame mapping. If the board mounting differs, heading will be systematically wrong. `MPU_SetMagAlignment()` is available but requires a calibrated reference.
3. **Soft-iron calibration defaults to identity (`valid=true`).** No correction is applied unless explicitly calibrated. This may introduce heading errors in the presence of ferromagnetic materials.
4. **I2C2 timing not independently verified.** `TIMINGR=0x00201313` targets 100 kHz at 4 MHz PCLK, but actual bus frequency depends on rise/fall times (cable length, pull-up strength, PCB capacitance). No scope measurement or STM32CubeMX timing calculator verification has been performed.
5. **Magnetometer calibration requires deliberate movement.** The 500-sample hard-iron calibration needs >10 µT range on all axes. On a static bench test, calibration will fail and heading will never become valid. This is by design (prevents false calibration) but must be accounted for in flight preparation.
6. **No I2C bus recovery.** Consecutive failures increment `consec_failures` but the driver never re-initializes the I2C peripheral or attempts bus clear. A stuck bus would require a full MCU reset.
7. **CPU2 RTC tick rate assumption (1024 Hz).** Freshness logic assumes `RTC_N_PREDIV_S` gives 1024 ticks/s. If the RTC prescaler configuration changes, the 500 ms age limit will be wrong.
8. **Flash image not committed.** The built `MXproject_CM4.bin/.elf/.hex` are in `build/` and must be flashed separately.

---

## 12. Summary

This integration adds a complete MPU-9250/MPU-6500 IMU chain to the existing GPS/LoRa payload:
- **Electrical diagnostics** at boot (I2C clock, GPIO config, bus idle, device probe at both addresses with WHO_AM_I and HAL/ISR capture).
- **Robust bring-up** with bounded retries, power-on delay, and graceful degradation (stops probing after 5 attempts).
- **100 Hz polling** with burst reads, complementary filter orientation, startup gyro bias calibration (stationary-only), and optional magnetometer hard-iron calibration (movement-required).
- **Seqlock shared memory** protocol with CPU2-side freshness watch (500 ms expiry on consumer's clock, first-observation gate).
- **Wire protocol v3** (26 bytes) combining GPS + MPU with strict bounds, model identification, and separate heading validity.
- **LoRa telemetry** with 10 s interval, 2 s ACK timeout, 3 retries, and 100 ms MPU freshness observation timer.
- **Full host test coverage** for driver logic, v1/v3 codec, and CPU2 snapshot consumer.

**Hardware status:** MPU-6500 at 0x68 fully validated end-to-end. MPU-9250 / magnetometer / heading path is code-complete but **unvalidated on hardware**.

---

*Generated for commit on branch `feature/ground-station`.*