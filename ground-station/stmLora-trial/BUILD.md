# STM32WL55JC2 LoRa Transceiver — Build & Flash Guide

## Overview

Bidirectional LoRa communication on NUCLEO-WL55JC2 using ST's official
SubGHz_Phy middleware. Two roles: **Initiator** (sends PING, waits for PONG)
and **Responder** (receives PING, sends PONG).

Based on ST's `SubGHz_Phy_PingPong` example for NUCLEO-WL55JC.

---

## Hardware

- **Board:** NUCLEO-WL55JC2 (470–520 MHz variant, CM4 core)
- **UART:** USART2 on ST-LINK VCP (115200 8N1)
- **USB:** ST-LINK USB (provides power + UART + debug)

> **IMPORTANT:** NUCLEO-WL55JC2 is the **470–520 MHz** variant.
> Do NOT configure for 868 MHz (EU) or 915 MHz (US). The antenna and
> RF matching are optimized for the JC2 band.

---

## Frequency & Power — Legal Notice

The default configuration (`RF_FREQUENCY = 470 MHz`, `TX_OUTPUT_POWER = 14 dBm`)
is an **example within the JC2 hardware band only**. Users **must**:

1. Identify an operating frequency that is **both** permitted in their jurisdiction
   **and** supported by the NUCLEO-WL55JC2's 470–520 MHz hardware band (including
   antenna and RF matching). If no such frequency exists for the JC2 board, **do
   not enable TX on this board**; use a hardware variant appropriate for a permitted
   band instead.
2. Select an output power compliant with local regulations.
3. Verify antenna and RF matching for the chosen frequency before enabling TX.

The default TX state depends on the preset:

- **Debug:** `APP_TX_ENABLED=1` (from `Inc/app_build_config.h`) — TX enabled.
- **Release:** `APP_TX_ENABLED=0` (CMake cache) — no transmission.
- **InitiatorTx / ResponderTx:** `APP_TX_ENABLED=1` (CMake cache) — TX enabled.

---

## Build

### Prerequisites

1. **ARM GCC toolchain** (14.x) — must be in `PATH` as `arm-none-eabi-gcc`.
   Download from [Arm GNU Toolchain](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads).
   After extraction, add the bin directory to `PATH`:
   ```bash
   export PATH=/path/to/gcc-arm-none-eabi-14.x/bin:$PATH
   ```
2. **CMake 3.20+** and **Ninja** (or Unix Makefiles)
3. **ST-Link utility** (`st-flash`) for flashing (optional)

### VS Code — Choosing a Preset

The project ships with four CMake presets in `CMakePresets.json`. **Always select a preset before building or flashing:**

| Preset          | Role        | TX   | Use case                     |
|-----------------|-------------|------|------------------------------|
| `Debug`         | Responder   | On   | Source-controlled config     |
| `Release`       | Initiator   | Off  | Optimized, receive-only      |
| `InitiatorTx`   | Initiator   | On   | Field test — sends PING      |
| `ResponderTx`   | Responder   | On   | Field test — replies PONG    |

In VS Code with the CMake Tools extension:
1. Click the preset selector in the status bar (shows `Debug` by default).
2. Choose `InitiatorTx` or `ResponderTx` for TX testing, or keep `Debug` for source-controlled config.
3. Click **Configure** (the preset's cache variables are applied automatically).
4. Click **Build** (or `Ctrl+Shift+B`).

#### Debug preset — source-controlled configuration

The `Debug` preset sets `APP_CONFIG_FROM_SOURCE=ON`, which tells CMake to read
role and TX settings from **`Inc/app_build_config.h`** instead of using CMake
cache variables. The header defaults are **Responder / TX-on**.

To change the Debug build:

1. Edit `Inc/app_build_config.h` (set `APP_ROLE_INITIATOR` and/or `APP_TX_ENABLED`).
2. Click **Configure** in VS Code (or run `cmake -S . -B build/Debug …` from the
   terminal — the preset supplies `APP_CONFIG_FROM_SOURCE=ON` automatically).
3. Click **Build**.

The other presets (`Release`, `InitiatorTx`, `ResponderTx`) set
`APP_CONFIG_FROM_SOURCE=OFF` and use CMake cache variables for role/TX.

That is all that is needed for the non-Debug presets — the preset fully specifies
`CMAKE_BUILD_TYPE`, `APP_ROLE_INITIATOR`, and `APP_TX_ENABLED`. No manual flags
or extra configuration is required.

You can also configure and build from the command line:
```bash
# Configure + build InitiatorTx (separate binary dir, TX enabled)
cmake -S . -B build/InitiatorTx -DCMAKE_BUILD_TYPE=Debug -DAPP_ROLE_INITIATOR=1 -DAPP_TX_ENABLED=1
cmake --build build/InitiatorTx

# Same for ResponderTx
cmake -S . -B build/ResponderTx -DCMAKE_BUILD_TYPE=Debug -DAPP_ROLE_INITIATOR=0 -DAPP_TX_ENABLED=1
cmake --build build/ResponderTx
```

> **Note:** The boot banner printed on UART (e.g. `TX: ENABLED` / `TX: DISABLED`)
> reflects the **compiled CMake flag**, not a runtime setting. Verify the banner
> matches your expectation before enabling TX.

### Build Commands

```bash
cd ground-station/stmLora-trial

# Debug: edit Inc/app_build_config.h first, then configure + build from project root
cmake --preset Debug
cmake --build --preset Debug

# Output files (in build/ directory):
#   stmLora-trial.elf  — ELF binary (for debugging)
#   stmLora-trial.bin  — Raw binary (for flashing)
#   stmLora-trial.hex  — Intel HEX (for flashing)
#   stmLora-trial.map  — Memory map
```

### Toolchain Consistency

The CMake configuration discovers `arm-none-eabi-gcc` from `PATH` at configure
time and records the full paths to all toolchain binaries in `CMakeCache.txt`.
**All linker flags are resolved by the compiler driver automatically** — no
hardcoded library paths remain in the CMake files. This means:

- **Changing toolchains requires reconfiguration.** If you switch the
  `arm-none-eabi-gcc` in `PATH` to a different installation or version, you
  **must** reconfigure: delete the existing `CMakeCache.txt` and
  `CMakeFiles/` directory (or delete the entire build directory) and run
  `cmake ..` again. Reconfiguring an existing build directory without cleaning
  will leave stale toolchain paths in the cache.

- **VS Code / CubeMX reconfigure:** If you regenerate the project from STM32
  CubeMX (which rewrites `CMakeLists.txt` and CMake presets), delete the
  build directory entirely and reconfigure from scratch.

### Role Selection (compile-time)

```bash
# Initiator — sends PING first (Release, InitiatorTx, ResponderTx presets)
cmake .. -DAPP_ROLE_INITIATOR=1

# Responder — waits for PING, replies PONG (Release preset)
cmake .. -DAPP_ROLE_INITIATOR=0
```

> **Debug preset:** Role comes from `Inc/app_build_config.h` (default: Responder).
> Edit the header, then reconfigure. CMake `-DAPP_ROLE_INITIATOR` is ignored
> when `APP_CONFIG_FROM_SOURCE=ON`.

### TX Control (compile-time)

#### Non-Debug presets (Release, InitiatorTx, ResponderTx)

```bash
# Disable TX (Release preset default) — firmware only receives and reports
cmake .. -DAPP_TX_ENABLED=0

# Enable TX (InitiatorTx / ResponderTx preset default) — for testing only
cmake .. -DAPP_TX_ENABLED=1
```

#### Debug preset

TX state comes from `Inc/app_build_config.h` (default: **enabled**).
Edit the header, then reconfigure. CMake `-DAPP_TX_ENABLED` is ignored
when `APP_CONFIG_FROM_SOURCE=ON`.

### CMake Cache Overrides — Important

The CMake configuration sets default values for `APP_ROLE_INITIATOR` and
`APP_TX_ENABLED` in `CMakeLists.txt`. **These defaults are stored in
`CMakeCache.txt` and persist across builds — but only when
`APP_CONFIG_FROM_SOURCE=OFF`.**

- **Debug preset (`APP_CONFIG_FROM_SOURCE=ON`):** CMake does NOT define
  `APP_ROLE_INITIATOR` or `APP_TX_ENABLED`. The values come entirely from
  `Inc/app_build_config.h`. The CMake cache variables are present but ignored
  by the compiler. Edit the header file, then reconfigure.
- **Other presets (`APP_CONFIG_FROM_SOURCE=OFF`):** CMake defines the macros
  from cache values. Changing `Inc/app_build_config.h` has NO effect — the
  CMake cache takes precedence.
- **Example:** If you previously configured with `-DAPP_TX_ENABLED=0`, the
  cache will contain `APP_TX_ENABLED:STRING=0`. With `APP_CONFIG_FROM_SOURCE=OFF`,
  this value is passed to the compiler regardless of what `app_build_config.h` says.
  Removing the `-D` flag from a new cmake command also does nothing — the cached
  value persists.
- **To change a setting** with a non-Debug preset: pass the `-D` flag explicitly,
  or delete the build directory and reconfigure from scratch.

**Recommended: always pass both flags explicitly for clarity:**

```bash
# Clean slate build — Initiator, TX disabled (safe default)
mkdir -p build/initiator_rx && cmake -S . -B build/initiator_rx -DCMAKE_BUILD_TYPE=Debug -DAPP_ROLE_INITIATOR=1 -DAPP_TX_ENABLED=0
cmake --build build/initiator_rx

# Separate initiator-TX and responder-TX builds — keep directories isolated
# Each role/TX combination needs its own build directory; do NOT reuse one
# build directory for two different roles or TX settings.
cmake -S . -B build/initiator_tx -DCMAKE_BUILD_TYPE=Debug -DAPP_ROLE_INITIATOR=1 -DAPP_TX_ENABLED=1
cmake -S . -B build/responder_tx -DCMAKE_BUILD_TYPE=Debug -DAPP_ROLE_INITIATOR=0 -DAPP_TX_ENABLED=1

# Build each image independently
cmake --build build/initiator_tx
cmake --build build/responder_tx
```

> **Important:** The two images above must be flashed to **different physical
> boards**. Before enabling TX (`APP_TX_ENABLED=1`), confirm that your chosen
> frequency and output power are legal in your jurisdiction and supported by
> the NUCLEO-WL55JC2's 470–520 MHz hardware band (see *Frequency & Power —
> Legal Notice* above).

**For VS Code / CMake Tools users:** If your `.vscode/cmake-kits.json` or
`.vscode/settings.json` hardcodes these flags, edit those files. The CMake
cache in `build/` will reflect whatever was configured at the last
`cmake configure` step.

### Frequency & RF Configuration

Edit `SubGHz_Phy/App/subghz_phy_app.h`:

```c
#define RF_FREQUENCY      470000000U  /* Hz — example within JC2 band (470-520 MHz) */
#define TX_OUTPUT_POWER   14          /* dBm — verify legal for your region */

/* LoRa parameters (ST defaults): */
#define LORA_BANDWIDTH    0           /* 0 = 125 kHz */
#define LORA_SPREADING_FACTOR  7     /* SF7 */
#define LORA_CODINGRATE      1       /* 4/5 */
#define LORA_PREAMBLE_LENGTH 8       /* 8 symbols */
```

**Compile-time validation:** The build will fail with an `#error` if
`RF_FREQUENCY` is outside the JC2 hardware band (470–520 MHz).

**Role/TX configuration:** The `Inc/app_build_config.h` header (used by the
Debug preset) controls `APP_ROLE_INITIATOR` and `APP_TX_ENABLED`. The other
presets use CMake cache variables instead.

---

## Flash

Flash the binary produced by your chosen preset to the appropriate board.
Each preset builds to a separate output directory:

| Preset          | Binary path                          | Board to flash        |
|-----------------|--------------------------------------|-----------------------|
| `Debug`         | `build/Debug/stmLora-trial.bin`      | Either (responder/TX) |
| `Release`       | `build/Release/stmLora-trial.bin`    | Either (RX only)      |
| `InitiatorTx`   | `build/InitiatorTx/stmLora-trial.bin` | Initiator board      |
| `ResponderTx`   | `build/ResponderTx/stmLora-trial.bin` | Responder board      |

### Using st-flash

```bash
# Example: flash the InitiatorTx binary
st-flash write build/InitiatorTx/stmLora-trial.bin 0x08000000

# Example: flash the ResponderTx binary
st-flash write build/ResponderTx/stmLora-trial.bin 0x08000000
```

### Using OpenOCD + GDB

```bash
openocd -f interface/stlink.cfg -f target/stm32wl.cfg
gdb-multiarch
  target extended-remote :3333
  mon reset halt
  load build/InitiatorTx/stmLora-trial.elf
  monitor reset halt
  quit
```

---

## UART Diagnostics

Connect to ST-LINK VCP port at **115200 8N1**:

```bash
# Linux
screen /dev/ttyACM0 115200
# macOS
screen /dev/cu.usbmodemXXXX 115200
# Or use any terminal: picocom, minicom, etc.
```

### Example Output (Debug preset — Responder, TX enabled)

The following is illustrative of what the firmware prints with the Debug preset
defaults (Responder, TX enabled). Actual values (RSSI, SNR, sequence numbers)
depend on the test setup.

```
  ----------------------------------------

  STM32WL55 LoRa Ping-Pong
  Board: NUCLEO-WL55JC2
  Role:  RESPONDER
  Freq:  470 MHz
  TX:    ENABLED
  BW:    125kHz SF7 CR4/5
  Preamble: 8 symbols
  ----------------------------------------
  Initializing SubGHz_Phy middleware...
  SubGHz_Phy initialized OK
  ----------------------------------------
  Starting RESPONDER task...
  TX is ENABLED. Waiting for radio events.
  ----------------------------------------
  [RX#1] seq=0 from=0x01 type=PING  RSSI=-60 SNR=8
  DATA: 4C 4F 52 41
  [TX] Sending PONG seq=0
```

### Example Output (Release preset — Initiator, TX disabled)

```
  ----------------------------------------

  STM32WL55 LoRa Ping-Pong
  Board: NUCLEO-WL55JC2
  Role:  INITIATOR
  Freq:  470 MHz
  TX:    DISABLED
  BW:    125kHz SF7 CR4/5
  Preamble: 8 symbols
  ----------------------------------------
  Initializing SubGHz_Phy middleware...
  SubGHz_Phy initialized OK
  ----------------------------------------
  Starting INITIATOR task...
  TX is DISABLED. Waiting for radio events.
  ----------------------------------------
  [RX#1] seq=0 from=0x02 type=PONG  RSSI=-55 SNR=9
  DATA: 4C 4F 52 41
```

### Example Output (Responder, TX disabled)

```
  ----------------------------------------

  STM32WL55 LoRa Ping-Pong
  Board: NUCLEO-WL55JC2
  Role:  RESPONDER
  Freq:  470 MHz
  TX:    DISABLED
  ...
  [RX#1] seq=0 from=0x01 type=PING  RSSI=-60 SNR=8
  DATA: 4C 4F 52 41
  [TX DISABLED] not transmitting PONG
```

---

## Message Protocol

```
[0]  Sync word:    0xA0
[1]  Sequence:     0–255 (wraps)
[2]  Sender ID:    0x01 (initiator) / 0x02 (responder)
[3]  Msg Type:     0x01=PING / 0x02=PONG
[4]  Payload len:  1–58 (zero-length rejected)
[5]  Reserved:     0x00
[6..63] Payload:  arbitrary data (echoed in PONG)
```

### Frame Validation

- **Minimum frame length:** 7 bytes (6-byte header + 1-byte payload)
- **Maximum frame length:** 64 bytes
- **Declared payload length:** must be 1–58 bytes (zero-length rejected)
- **Length consistency:** `frame_length == 6 + declared_payload_length`
- **Sync word:** must be 0xA0
- **Message type:** must be PING (0x01) or PONG (0x02)
- **Reserved byte:** must be 0x00
- Invalid frames are rejected, counted as errors, and RX is re-armed.

### Protocol Behavior

- **Initiator:** Uses a **one-shot PING timer** (`timerPingOneShot`).
  After the timer fires and a PING is sent (or TX is disabled), the timer
  is re-armed for the next exchange. No fixed periodic cadence is assumed —
  the interval is `APP_CYCLE_DELAY_MS` (default 2000 ms) but the timer is
  explicitly one-shot and re-started per exchange.
- **Responder:** Listens for PING. On receiving PING from initiator (0x01),
  echoes PONG with the same sequence number and payload bytes.
- **PONG match:** Initiator validates PONG sender (0x02), sequence, type,
  length, and payload against the outstanding PING. Mismatches are ignored.
- **TX guard:** When `APP_TX_ENABLED=0`, no `Radio.Send()` is called on
  any code path (normal, timeout, error, retry). The firmware only
  receives and reports via UART.
- **Recovery:** RX timeout, TX timeout, and RX error all clear the pending
  PING flag, set `radio_rearm_pending`, and schedule `PingPong_Process()`
  to re-arm RX via the event flag — the radio never permanently stops
  listening.
- **Test payload:** Default PING payload is "LORA" (4 bytes). The responder
  echoes the exact bytes back.

### State Machine

```
Initiator:  RX → TX_PING → WAIT_PONG → RX → TX_PING → ...
Responder:  RX → TX_PONG → RX → TX_PONG → ...

On timeout/error: → RX (via radio_rearm_pending flag)
```

- After any `Radio.Send()`, the process function returns immediately —
  it never calls `RadioRx()` in the same invocation (avoids aborting TX).
- `OnTxDone` sets the target state and `radio_rearm_pending`, then schedules
  `PingPong_Process` via the sequencer.
- `OnRxTimeout`, `OnTxTimeout`, and `OnRxError` all set
  `radio_rearm_pending` and schedule `PingPong_Process` to re-arm RX.
- `PingPong_Process()` consumes `radio_rearm_pending` exactly once per
  invocation — timer-only wakes do not re-arm RX.

---

## Radio Configuration

| Parameter          | Value        |
|--------------------|--------------|
| Modulation         | LoRa         |
| Bandwidth          | 125 kHz      |
| Spreading Factor   | 7            |
| Coding Rate        | 4/5          |
| Preamble Length    | 8 symbols    |
| Frequency (JC2)    | 470 MHz*     |
| TX Power           | +14 dBm*     |

\* Examples within JC2 hardware band — users must select legally permitted
values for their region.

---

## File Structure

```
stmLora-trial/
├── Src/
│   ├── main.c                  ← Application (init + main loop)
│   ├── system_stm32wlxx.c      ← CMSIS system init (ST original)
│   ├── startup_stm32wl55jcix.s ← Startup (ST original)
│   ├── syscalls.c, sysmem.c    ← Minimal syscalls (ST original)
│   ├── stm32wlxx_it.c          ← Interrupt handlers (ST)
│   ├── stm32wlxx_hal_msp.c     ← HAL MSP (ST)
│   ├── subghz.c                ← SUBGHZ SPI init (ST)
│   ├── usart.c, usart_if.c     ← UART (ST)
│   ├── gpio.c                  ← GPIO init (ST)
│   ├── timer_if.c              ← Timer (ST)
│   ├── sys_app.c               ← System app (ST)
│   └── ...
├── SubGHz_Phy/
│   ├── App/
│   │   ├── subghz_phy_app.c    ← Radio app layer (ST, adapted)
│   │   ├── subghz_phy_app.h    ← Radio config (JC2 freq/power)
│   │   └── app_subghz_phy.c    ← MX wrapper (ST original)
│   └── Target/
│       ├── radio_board_if.c    ← Board radio interface (ST)
│       └── radio_conf.h        ← Radio config (ST)
├── Middlewares/
│   └── SubGHz_Phy/             ← ST SubGHz_Phy middleware (exact ST)
│       ├── radio_driver/       ← radio.c, radio_driver.c (exact ST)
│       └── ...
├── Drivers/
│   ├── BSP/STM32WLxx_Nucleo/   ← ST BSP (exact ST)
│   ├── CMSIS/Device/ST/        ← CMSIS device headers (copied from ST)
│   └── STM32WLxx_HAL_Driver/   ← ST HAL (exact ST)
├── CMSIS_6/                    ← ARM CMSIS 6 core headers (exact ST)
├── Utilities/
│   ├── sequencer/              ← ST sequencer (exact ST)
│   ├── trace/adv_trace/        ← ST advanced trace (exact ST)
│   ├── timer/                  ← ST timer (exact ST)
│   ├── lpm/tiny_lpm/           ← ST low-power management (exact ST)
│   └── misc/                   ← ST utility functions (exact ST)
├── cmake/
│   ├── components.cmake        ← Source file list
│   ├── flags.cmake             ← Includes, defines, linker
│   ├── target.cmake            ← CPU/core config
│   └── gcc-arm-none-eabi.cmake ← Toolchain config
├── CMakeLists.txt              ← Top-level CMake
├── stm32wl55xc_flash_cm4.ld    ← Linker script (ST original)
└── BUILD.md                    ← This file
```

---

## Build Artifacts

The following files are generated in the preset's `build/` subdirectory:

| Preset          | ELF path                                  | BIN path                                  |
|-----------------|-------------------------------------------|-------------------------------------------|
| `Debug`         | `build/Debug/stmLora-trial.elf`           | `build/Debug/stmLora-trial.bin`           |
| `Release`       | `build/Release/stmLora-trial.elf`         | `build/Release/stmLora-trial.bin`         |
| `InitiatorTx`   | `build/InitiatorTx/stmLora-trial.elf`     | `build/InitiatorTx/stmLora-trial.bin`     |
| `ResponderTx`   | `build/ResponderTx/stmLora-trial.elf`     | `build/ResponderTx/stmLora-trial.bin`     |

The `.tmp/` directory and `build/` directory should be ignored by git.
Unrelated files (`.vscode/`, `.clangd`, `.settings/`, `project-description.json`,
`loraimplement.md`) are preserved.

---

## Summary of Changes

### CMSIS Device Headers
- Copied official `stm32wl55xx.h`, `stm32wlxx.h`, `system_stm32wlxx.h`
  from `/tmp/stm32cube-offline-source/cmsis-device-wl-main/Include/`
- Copied official `startup_stm32wl55jcix.s` from PingPong example
- Copied official `system_stm32wlxx.c` from cmsis-device-wl

### CMake Configuration
- Consolidated `cmake/components.cmake` and `cmake/files.cmake` into a
  single coherent source list
- Removed references to missing/non-existent files
- Added all actual source files from the project
- Fixed include paths to point to correct CMSIS device directory
- Added `add_custom_command` for BIN/HEX generation
- Added `APP_CONFIG_FROM_SOURCE` option (default OFF): when ON, role/TX
  come from `Inc/app_build_config.h` instead of CMake cache variables
- Propagated `APP_ROLE_INITIATOR` and `APP_TX_ENABLED` via CMake -D flags
  (only when `APP_CONFIG_FROM_SOURCE=OFF`)
- **Toolchain file:** removed hardcoded path; uses `arm-none-eabi-*` from PATH

### Main Application
- Removed duplicate radio logic, local callbacks, and unused HAL_GetTick
- Full ST init sequence: HAL_Init → SystemClock_Config → MX_GPIO_Init
  → MX_USART2_UART_Init → vcom_Init → MX_SubGHz_Phy_Init
- Main loop calls MX_SubGHz_Phy_Process() (ST sequencer)
- Application messaging (PING/PONG protocol) lives in SubGHz_Phy/App layer
- Bounded payloads (64 bytes max), sequence numbers, RSSI/SNR reporting
- Length validation (rejects truncated/overlong/empty)
- TX done/timeout and RX timeout/error recovery
- BOTH roles send and receive
- **Centralized config:** `Inc/app_build_config.h` provides role/TX defaults
  for the Debug preset; CMake cache variables for other presets

### FSM / Callback Model
- Replaced busy `PingPong_Process()` self-queue with callback/timer-driven FSM
- Radio callbacks (OnTxDone, OnRxDone, OnTxTimeout, OnRxError) queue the
  sequencer task — never self-schedule
- One-shot PING timer (`timerPingOneShot`, `UTIL_TIMER_ONESHOT`) — re-armed
  after each exchange or timeout, never free-running
- State machine transitions: RX → TX_PING → WAIT_PONG → RX (on initiator)
  or RX → TX_PONG → RX (on responder)
- State transitions are driven by radio callbacks setting `AppState` and
  setting `radio_rearm_pending`; `PingPong_Process()` consumes the
  `radio_rearm_pending` event flag exactly once per invocation to call
  `RadioRx()` — timer-only wakes do not re-arm RX.
- No `Radio.Sleep()` at start of `PingPong_Process()` — RX is retained
- **Critical fix:** After any `Radio.Send()`, `PingPong_Process()` returns
  immediately — it never falls through to `RadioRx()` in the same call

### PING/PONG Protocol
- PING sends gated by `APP_ROLE_INITIATOR` and `ping_pending` flag —
  responder never sends PING; timer flag consumed only on send
- PONG echoes received PING's sequence number and payload exactly
- Initiator validates PONG against outstanding PING (sender=0x02, seq,
  type, length, payload match)
- `tx_seq` advanced only on new PING send, not on PONG echo
- `tx_count` incremented only on actual TX completion (OnTxDone)

### MsgDeserialize Validation
- Rejects unless `7 <= frame_length <= 64` (header + min 1-byte payload)
- Declared payload length must be 1..58 (zero-length rejected)
- frame_length must equal 6 + declared_length
- Validates type (PING/PONG) and reserved byte (0x00)
- OnRxDone validates size before narrowing uint16_t → uint8_t
- Overlong payloads rejected and recovered without reading uninitialized data
- Full payload (up to 58 bytes) and RSSI/SNR printed on UART

### Centralized TX
- All TX goes through `RadioSendWithLength(buf, len)` with actual serialized length
- `APP_TX_ENABLED=0` compiles away all `Radio.Send()` paths
- No transmission on timeout when TX disabled
- Fixed test payload "LORA" (4 bytes) replaces literal PING/PONG strings
- `APP_TX_ENABLED` defaults come from `Inc/app_build_config.h` (Debug) or
  CMake cache (other presets), not from duplicated `#ifndef` blocks in source

### TX Guard
- All TX paths in subghz_phy_app.c guarded by `#if APP_TX_ENABLED != 0`
- Debug preset default: APP_TX_ENABLED=1 (from `app_build_config.h`)
- Release preset default: APP_TX_ENABLED=0 (CMake cache)
- Compile-time rejection of frequencies outside JC2 band (470–520 MHz)
- Configured: BW125 (LORA_BANDWIDTH=0), SF7, CR4/5, preamble 8

### BSP Driver
- USE_BSP_DRIVER defined in platform.h
- Official Nucleo BSP files linked (stm32wlxx_nucleo.c, stm32wlxx_nucleo_radio.c)
- Startup → SUBGHZ IRQ → Radio callback path follows source wiring

### BUILD.md
- Accurate build commands with correct CMake flags
- Correct output paths (preset-specific build directories)
- Correct flash command with preset-specific binary paths
- Accurate protocol descriptions matching source code
- Updated protocol: sender IDs, payload validation, state machine
- Updated toolchain instructions (PATH-based, no hardcoded paths)
