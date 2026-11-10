# SubGHz_Phy PingPong on CPU2 (NUCLEO-WL55JC2)

This directory holds the STM32CubeWL `SubGHz_Phy_PingPong` application, ported to
CPU2 (Cortex-M0+) of this dual-core CubeMX/CMake project. CPU1 (Cortex-M4) keeps
its CubeMX role of boot master.

## 1. What the application does

Two boards exchange a PING/PONG sequence on the Sub-GHz radio (upstream
behaviour, unchanged):

- Each board starts in the `RX` state and waits for a frame.
- A board that receives `"PING"` becomes the **slave** and answers `"PONG"`.
- A board that receives `"PONG"` (or any unknown payload) is/stays the
  **master** and sends the next `"PING"`.
- On RX timeout / RX error the master re-sends `"PING"`, the slave re-arms RX.
- TX payload: 64 bytes (`PAYLOAD_LEN`), first bytes `PING`/`PONG`, rest zeroed.
- LED2 (green) blinks while no peer is heard; after a successful exchange the
  master toggles LED3 (red) and the slave toggles LED2 (green) per PING/PONG.
- Trace on the ST-LINK VCP (USART2, PA2/PA3, 115200 8N1) reports the version
  banner, each radio event, payload bytes, RSSI and (LoRa) SNR.

Role selection is the upstream self-arbitration, there is no compile-time role
switch: flash the same image on both boards.

Radio configuration (`SubGHz_Phy/App/subghz_phy_app.h`):

| Parameter | Value |
|---|---|
| Frequency | **470 MHz** (see deviation D1) |
| Modem | LoRa, BW 125 kHz, SF7, CR 4/5, preamble 8, variable length |
| TX power | 14 dBm (low-power PA path, board limit 15 dBm) |
| RX/TX timeout | 3000 ms |

## 2. Provenance and versions

All imported code is STMicroelectronics source, copied unmodified except where a
CPU2 adaptation is listed in section 4.

| Path | Upstream | Version |
|---|---|---|
| `SubGHz_Phy/App/*`, `SubGHz_Phy/Target/*` | STM32CubeWL `Projects/NUCLEO-WL55JC/Applications/SubGHz_Phy/SubGHz_Phy_PingPong` | v1.6.0 |
| `Middlewares/SubGHz_Phy/radio_driver/*` | `stm32-mw-subghz-phy` (CubeWL submodule `Middlewares/Third_Party/SubGHz_Phy`) | v1.5.0 |
| `Utilities/{misc,timer,sequencer,lpm/tiny_lpm,trace/adv_trace}` | STM32CubeWL `Utilities/` | v1.6.0 |
| `../Drivers/STM32WLxx_HAL_Driver/{Inc,Src}/stm32wlxx_hal_subghz.*`, `stm32wlxx_hal_rtc*.h/.c`, `stm32wlxx_ll_rtc.h`, `stm32wlxx_ll_spi.h` | `stm32wlxx_hal_driver` (version already used by this project) | v1.6.0 |
| `../Drivers/BSP/STM32WLxx_Nucleo/stm32wlxx_nucleo_radio.{c,h}` | `stm32wlxx-nucleo-bsp` (BSP version pinned by CubeWL v1.6.0) | v1.3.1 |

Licences travel with the code: `LICENSE`/`LICENSE.md` in
`Middlewares/SubGHz_Phy/`, `Utilities/*/` and `../Drivers/BSP/STM32WLxx_Nucleo/`,
plus the ST SLA0044 header in every file.

Files that are *not* copied from upstream were hand-adapted and are listed in
section 4. The CubeMX-generated CMake files (`CM0PLUS/mx-generated.cmake`,
`CM0PLUS/CMakeLists.txt`) are untouched except for one `include()` line that
pulls in `CM0PLUS/subghz_phy.cmake`, which holds all added source/include
definitions so that CubeMX regeneration does not drop them. The top-level
`mx-generated.cmake` additionally carries the per-build-type sub-build override
described in 5.4 and 7; it also applies to the CPU2-only build context.

## 3. Dual-core architecture

| Resource | Owner | Notes |
|---|---|---|
| System clocks (MSI 4 MHz, AHB/APB dividers, HCLK3) | CPU1 | CPU2 has no `SystemClock_Config()`; reconfiguring the clock tree from CPU2 would race with CPU1 |
| CPU2 boot | CPU1 | `HAL_PWREx_ReleaseCore(PWR_CORE_CPU2)` at the end of CPU1 `main()` (already present) |
| SUBGHZ radio + SUBGHZSPI + RF switch (PC3/PC4/PC5) | CPU2 | `SUBGHZ_Radio_IRQn` (IRQ 31 on CPU2, 50 on CPU1); `HAL_SUBGHZ_Init()` unmasks the radio IRQ for CPU2 through `LL_C2_EXTI_EnableIT_32_63(LL_EXTI_LINE_44)` |
| RTC (UTIL_TIMER / HAL time base) | CPU2 | CPU2 has one combined RTC vector `RTC_LSECSS_IRQn`; the HAL enables `EXTI->C2IMR1` for alarm A. CPU1 must not use the RTC while this image runs |
| USART2 PA2/PA3 (VCP) + DMA1 channel 5 | CPU2 | CPU2 vector names: `USART2_IRQn`, `DMA1_Channel4_5_6_7_IRQn` |
| USART1 PB7 (GNSS receiver) | CPU1 | RX only, 9600 8N1, interrupt reception; the lines reach the VCP through the SRAM1 ring of 3.1 |
| Low power | CPU2 sleeps only (`WFI`) | `LOW_POWER_DISABLE 1`: platform Stop 2 entry needs a CPU1/CPU2 IPCC+HSEM handshake that this project does not implement |
| HAL time base | RTC (LSE) | `sys_app.c` overrides `HAL_InitTick`/`HAL_GetTick`/`HAL_Delay`; SysTick is never enabled on CPU2, so ping-pong timings do not depend on the CPU clock |

Interrupt vector names differ between the two cores. The handlers implemented in
`Core/Src/stm32wlxx_it.c` use the **CPU2** names found in
`Core/Startup/startup_stm32wl55xx_cm0plus.s`:

| Function | CPU2 IRQ | Wraps |
|---|---|---|
| `RTC_LSECSS_IRQHandler` | `RTC_LSECSS_IRQn` | `HAL_RTCEx_SSRUIRQHandler()` + `HAL_RTC_AlarmIRQHandler()` |
| `DMA1_Channel4_5_6_7_IRQHandler` | `DMA1_Channel4_5_6_7_IRQn` | `HAL_DMA_IRQHandler(&hdma_usart2_tx)` |
| `USART2_IRQHandler` | `USART2_IRQn` | `HAL_UART_IRQHandler(&huart2)` |
| `SUBGHZ_Radio_IRQHandler` | `SUBGHZ_Radio_IRQn` | `HAL_SUBGHZ_IRQHandler(&hsubghz)` |

### 3.1 GNSS sentences on the ST-LINK VCP

CPU1 receives the SAM-M10Q NMEA stream on USART1 (PB7, 9600 8N1) while CPU2
owns USART2 and its DMA channel, so the sentences travel through a 512-byte
ring in SRAM1 instead of through the UART:

| Part | Where |
|---|---|
| Ring and protocol (`GnssVcpMailbox`) | `../Common/GnssVcp/gnss_vcp_mailbox.h`; 1 KB at `0x20003C00`, carved out of CPU1's RAM1 (`../CM4/STM32WL55XX_FLASH_CM4.ld` is 15 KB, not 16 KB) |
| Producer | `../CM4/Core/Src/gnss_vcp.c`: CPU1 main loop copies each complete line + CR LF into the ring; it never transmits |
| Consumer | `Core/Src/gnss_vcp_trace.c`: a 50 ms `UTIL_TIMER` only schedules the `CFG_SEQ_Task_Gnss_Vcp_Trace` sequencer task, which prints whole lines through the advanced trace (DMA) |

Nothing is printed from either interrupt, and neither core ever touches the
other's UART. Each received sentence is printed verbatim as one CR LF
terminated line at the medium verbose level (`VLEVEL_M`), without a timestamp,
so the monitor output stays pasteable NMEA; no output is produced while no
complete line has been received. Serial monitor: **115200 8N1** on the ST-LINK
VCP (PA2/PA3), the same port that carries the ping-pong trace.

Loss is counted, never hidden: `gnss_vcp_lines_dropped` (CPU1, ring full because
CPU2 is not draining, e.g. no CPU2 image) and `gnss_vcp_trace_dropped` (CPU2,
trace FIFO full).

## 4. Deviations from the upstream example (all deliberate)

| Id | Change | Reason |
|---|---|---|
| D1 | `RF_FREQUENCY` 868 MHz → **470 MHz** | The upstream example targets NUCLEO-WL55JC1 (868 MHz). This project's board is NUCLEO-WL55JC2, whose front-end filter is the 470 MHz band variant (same choice as the existing `stmLora-trial` firmware, which was verified on hardware at 470 MHz). Everything else (payload, modulation, state machine) is upstream. |
| D2 | `sys_conf.h`: `LOW_POWER_DISABLE` 0 → **1** | Avoids platform Stop 2 without CPU1/CPU2 low-power coordination; CPU2 uses sleep/WFI only. |
| D3 | IRQ handler names (see table above) | CPU2 vector table aggregates RTC events and DMA1 ch. 4-7. |
| D4 | `rtc.c` MSP: single `RTC_LSECSS_IRQn` | `TAMP_STAMP_LSECSS_SSRU_IRQn`/`RTC_Alarm_IRQn` do not exist on CPU2. |
| D5 | `dma.c`, `usart_if.c`: `DMA1_Channel4_5_6_7_IRQn` | Same as D3 for the DMA channel 5 vector. |
| D6 | `usart_if.c`: `LL_C2_EXTI_EnableIT_0_31()` for the USART2 wake-up line | The wake-up line must be unmasked for CPU2 (`EXTI->C2IMR1`), not for CPU1. |
| D7 | `stm32_lpm_if.c`: `LL_PWR_ClearFlag_C2STOP_C2STB()` under `CORE_CM0PLUS` | CPU2 stop flag instead of CPU1's. |
| D8 | `sys_debug.c`: DBGMCU calls skipped on CPU2 | The HAL only provides `HAL_DBGMCU_*` for CPU1; the registers stay at their reset value (equivalent to `DEBUGGER_ENABLED 0`). |
| D9 | `gpio.c`: only the three LEDs are initialised | The buttons are unused by PingPong and CPU2 registers no EXTI handler for them. SW probes (PB12/PB13) are configured by `DBG_Init()`, RF switch pins by the radio BSP. |
| D10 | `main.c`: no `SystemClock_Config()` | CPU1 owns the clock tree (section 3). |
| D11 | Template files of the utilities (`*_template.c/h`) and `_htmresc`/`Release_Notes` omitted | Not compiled / not needed; keeps the tree minimal. |

## 5. Build and flash

### 5.1 Toolchain prerequisite

The project needs an `arm-none-eabi-gcc` toolchain **with newlib**, plus `cmake`
>= 3.22 and `ninja`. The build resolves the compiler from `PATH` (see
`gcc-arm-none-eabi.cmake`), so the toolchain has to be put first explicitly.

The `arm-none-eabi-gcc` currently installed by Homebrew on this machine
(16.2.0) is packaged **without newlib**, so it fails on *any* C file that
includes a standard header (`#include_next <stdint.h>`, `errno.h`, ...), the
plain CubeMX CPU1 baseline included. This is a host packaging problem, not a
project one; it is why the default `PATH` does not build this tree. Validated
with **ARM GNU Toolchain 13.3.Rel1** (macOS arm64).

```sh
# ARM GNU Toolchain 13.3.Rel1 unpacked on this machine; any install prefix works
export PATH=/tmp/arm-gnu-toolchain-13.3.rel1-darwin-arm64-arm-none-eabi/bin:$PATH

# must print an absolute path inside that toolchain, not the bare name "libc.a"
arm-none-eabi-gcc -print-file-name=libc.a
```

If the second command prints just `libc.a`, the compiler found first in `PATH`
has no newlib (this is the Homebrew failure above): every compile will die at
`stdint.h`. Fix `PATH` before building.

### 5.2 Build both cores

```sh
cd MXproject
# see 5.1 for PATH

# Debug
cmake --preset Debug && cmake --build --preset Debug

# Release
cmake --preset Release && cmake --build --preset Release

# and back to Debug at any time, without deleting anything first
cmake --preset Debug && cmake --build --preset Debug
```

Each preset has its own wrapper directory (`build/<Preset>`) and each core its
own build-type sub-build directory (`CM4/build/<Config>`,
`CM0PLUS/build/<Config>`), so the two configurations coexist in the tree and
switching presets never reuses the other configuration's objects or image (5.4).
There is no cleanup step between configurations.

Expected result with the toolchain above: no warnings and no errors (`-Wall` is
enabled) and this linker summary from `--print-memory-usage`:

| Preset | Core | ROM | RAM1 | RAM2 |
|---|---|---|---|---|
| Debug | CPU2 (CM0PLUS) | 52432 B = 40.00 % of 128 KB | 3752 B = 22.90 % of 16 KB | 0 B |
| Debug | CPU1 (CM4) | 12072 B = 9.21 % of 128 KB | 1824 B = 11.13 % of 16 KB | 0 B |
| Release | CPU2 (CM0PLUS) | 28480 B = 21.73 % of 128 KB | 3744 B = 22.85 % of 16 KB | 0 B |
| Release | CPU1 (CM4) | 6920 B = 5.28 % of 128 KB | 1776 B = 10.84 % of 16 KB | 0 B |

CPU2 only (skips the CM4 sub-build):

```sh
cmake -S . -B build/cm0plus-only -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE=$PWD/gcc-arm-none-eabi.cmake \
      -DBUILD_CONTEXT=CM0PLUS -DCMAKE_BUILD_TYPE=Debug
cmake --build build/cm0plus-only
```

This only builds `CM0PLUS/build/Debug`, the same sub-build directory and
configuration the full Debug preset uses, so the two commands produce the same
CPU2 image.

### 5.3 Artifacts

| Core | Image (Debug / Release) | Flash (Debug / Release) | RAM (Debug / Release) |
|---|---|---|---|
| CPU2 | `CM0PLUS/build/Debug/MXproject_CM0PLUS.elf` / `CM0PLUS/build/Release/MXproject_CM0PLUS.elf` → 0x08020000 | 52432 B / 28480 B of 128 KB | 3752 B / 3744 B of 16 KB |
| CPU1 | `CM4/build/Debug/MXproject_CM4.elf` / `CM4/build/Release/MXproject_CM4.elf` → 0x08000000 | 12072 B / 6920 B of 128 KB | 1824 B / 1776 B of 16 KB |

Both images must be flashed: CPU1 releases CPU2, CPU2 runs the radio.

Off-target cross-check of the CPU2 Debug image (no hardware needed):
`arm-none-eabi-objdump -s -j .isr_vector CM0PLUS/build/Debug/MXproject_CM0PLUS.elf`
starts with `00800020` (initial SP = `_estack`) and the entries at byte offsets
0x48, 0x64, 0xb0 and 0xbc hold the `RTC_LSECSS_IRQHandler`,
`DMA1_Channel4_5_6_7_IRQHandler`, `USART2_IRQHandler` and
`SUBGHZ_Radio_IRQHandler` addresses (IRQ 2, 9, 28, 31) — i.e. the handlers are
really wired, not `Default_Handler`.

### 5.4 Build state: per-build-type sub-build directories

The Cortex-M4 and Cortex-M0+ projects are built by the `ExternalProject_Add`
rules of the top-level `mx-generated.cmake`, whose `BINARY_DIR` arguments CubeMX
pins to `CM4/build` and `CM0PLUS/build`. With those fixed paths both presets
shared one sub-build per core, and because a wrapper directory (`build/Debug`,
`build/Release`) that has been configured once never re-runs its
`ExternalProject` configure step, switching presets silently kept the previous
configuration: after a `Release` build, `cmake --preset Debug && cmake --build
--preset Debug` relinked a Release image (and vice versa), visible only in the
`--print-memory-usage` numbers of 5.2.

The `BINARY_DIR` paths are therefore redirected to a per-build-type directory by
a local override at the top of `mx-generated.cmake` (the `MX_BUILD_TYPE_DIR`
block; see 7 for the regeneration caveat):

| Preset | Wrapper project | CPU1 sub-build | CPU2 sub-build |
|---|---|---|---|
| `Debug` | `build/Debug` | `CM4/build/Debug` | `CM0PLUS/build/Debug` |
| `Release` | `build/Release` | `CM4/build/Release` | `CM0PLUS/build/Release` |

Consequences:

- Switching presets in either direction rebuilds the requested configuration;
  the other configuration's directories are left untouched, so no `rm -rf` is
  needed between builds.
- `cmake --build --preset <Preset> --target clean` removes only that
  configuration's sub-build directories (`ADDITIONAL_CLEAN_FILES`).
- The layout matches the `build/${presetName}` sub-build directories of the
  core projects' own `CM4`/`CM0PLUS` `CMakePresets.json`, so a standalone core
  build and the top-level build share one cache and artifact. Alternating the
  two ways of configuring the same directory makes CMake print its cosmetic
  `Manually-specified variables were not used by the project:
  CMAKE_TOOLCHAIN_FILE` note; it does not affect the build.

Remaining stale-state caveat: **toolchain**. The compiler path is recorded in
the sub-build `CMakeCache.txt` and a reconfigure does **not** pick up a
different `arm-none-eabi-gcc` from `PATH`; the symptom is a build that still
fails inside `/opt/homebrew/.../stdint.h` after `PATH` was fixed. Delete the
affected configuration's sub-build directories when the toolchain changes:

```sh
rm -rf CM4/build/Debug CM0PLUS/build/Debug      # or CM4/build, CM0PLUS/build
```

Trees created before this layout still contain the old shared
`CM4/build`/`CM0PLUS/build` caches next to the new `Debug`/`Release`
directories. They are ignored, untracked and safe to delete once
(`rm -rf build CM4/build CM0PLUS/build`); it does not build anything else.

## 6. Hardware validation still required (not done here)

Nothing in this port has been run on hardware; only compilation, linking and the
CPU2 vector-table wiring were verified. Remaining bench work:

1. Flash both cores on two NUCLEO-WL55JC2 boards, confirm the CPU2 banner
   (`PING PONG`, `APPLICATION_VERSION`, `MW_RADIO_VERSION`, `LORA_MODULATION`,
   `rand=…`) appears on the VCP of both boards, and that the NMEA sentences of
   a receiver streaming on USART1/PB7 follow it one line at a time (3.1).
2. Confirm one board logs `Master Tx start`/`OnTxDone` and the other
   `OnRxDone`/`PONG`, with the LEDs toggling — i.e. that CPU2 really completes
   radio IRQ → sequencer task → TX/RX cycles.
3. Check RSSI/SNR values are plausible, and that the link survives the 3 s RX
   timeout / re-synchronisation path (`RX_TIMEOUT` → master re-sends PING).
4. Confirm CPU1 is not disturbed: CPU1 must not fault or re-enter `main()` when
   CPU2 takes USART2/RTC/SUBGHZ interrupts.
5. Interoperability with a stock upstream example build requires re-flashing that
   board to 470 MHz (D1); the frame format itself is unchanged.
6. Not validated: 915 MHz operation (the flight radio is a LAMBDA62C-9S/SX1262),
   antenna matching at 470 MHz, current consumption in sleep mode.

## 7. Regenerating from MXproject.ioc

- `CM0PLUS/CMakeLists.txt` gets one `include("subghz_phy.cmake")`; if CubeMX
  rewrites the file, re-add that line. `mx-generated.cmake` and
  `subghz_phy.cmake` are otherwise independent.
- The top-level `MXproject/mx-generated.cmake` carries the per-build-type
  `BINARY_DIR` override of 5.4: the `MX_BUILD_TYPE_DIR` block between the
  `File automatically-generated` header comment and
  `set(ST_MULTICONTEXT ...)`, plus the three lines using
  `${MX_CM4_BUILD_DIR}` / `${MX_CM0PLUS_BUILD_DIR}` (`BINARY_DIR`,
  `ADDITIONAL_CLEAN_FILES`, `ST_MULTI_CONTEXT_*_PROJECT_BUILD_TARGET`) in each
  build-context block. If CubeMX regenerates the file, re-apply the block;
  without it the sub-builds silently return to the shared `CM4/build` and
  `CM0PLUS/build` paths, where switching presets reuses the wrong build type
  (5.4).
- `MXproject.ioc` still describes CPU1 owning the VCP: CPU1's generated
  `main()` initialises the ST-LINK VCP (BSP COM1 = **LPUART1 on PA2/PA3**) before
  releasing CPU2, and CPU2 then reconfigures PA2/PA3 as USART2. It works because
  CPU1's initialisation happens first and CPU1 never transmits, but the clean
  setting is to disable the VCP peripheral on CPU1 (or move the CPU2 trace to a
  free UART) when the `.ioc` is next edited.
- The `.ioc` does not list the RTC, USART2-on-CPU2, SUBGHZ or the SubGHz_Phy
  middleware, so CubeMX regeneration will not recreate
  `Core/Src/{rtc,usart,dma,subghz,gpio}.c`, `Core/Src/{sys_app,sys_debug,
  usart_if,timer_if,stm32_lpm_if}.c` or `subghz_phy.cmake`; they are plain
  source files in the tree and survive regeneration as long as "delete previous
  generated files" does not remove unlisted files.
