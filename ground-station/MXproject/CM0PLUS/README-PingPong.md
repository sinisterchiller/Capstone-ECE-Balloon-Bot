# SubGHz_Phy PingPong on CPU2 (NUCLEO-WL55JC2)

This directory holds the STM32CubeWL `SubGHz_Phy_PingPong` application, ported to
CPU2 (Cortex-M0+) of this dual-core CubeMX/CMake project. CPU1 (Cortex-M4) is
the boot master and releases CPU2 at the end of its `main()`.

## 1. What the application does

Two boards exchange a PING/PONG sequence on the Sub-GHz radio with **compile-time
roles**:

- **PAYLOAD** image (FW_ROLE=PAYLOAD): Master — sends `"PING"` every 5 s.
- **GROUND** image (FW_ROLE=GROUND): Slave — waits for `"PING"`, replies `"PONG"`.

Frame format (64 bytes, `PAYLOAD_LEN`):
- Bytes 0–3: `"PING"` (0x50 0x49 0x4E 0x47) or `"PONG"` (0x50 0x4F 0x4E 0x47)
- Bytes 4–5: packet counter, little-endian, increments per transmission
- Bytes 6–63: zero padding

Radio configuration (NUCLEO-WL55JC2 front-end is the 470 MHz band variant):

| Parameter | Value |
|---|---|
| Frequency | **470 MHz** |
| Modem | LoRa, BW 125 kHz, SF7, CR 4/5, preamble 8, variable length |
| TX power | 14 dBm (low-power PA path, board limit 15 dBm) |
| RX/TX timeout | 3000 ms |

RTC (LSE) provides the HAL time base; USART2 (PA2/PA3, 115200 8N1) on the ST-LINK VCP
logs version banner, radio events, payload bytes, RSSI and SNR.

## 2. Dual-core architecture

| Resource | Owner | Notes |
|---|---|---|
| System clocks | CPU1 | CPU2 has no `SystemClock_Config()` |
| CPU2 boot | CPU1 | `HAL_PWREx_ReleaseCore(PWR_CORE_CPU2)` at end of CPU1 `main()` |
| SUBGHZ radio + RF switch (PC3/PC4/PC5) | CPU2 | `SUBGHZ_Radio_IRQn` (IRQ 31 on CPU2) |
| RTC (UTIL_TIMER / HAL time base) | CPU2 | CPU2 vector `RTC_LSECSS_IRQn`; CPU1 must not use RTC |
| USART2 PA2/PA3 (VCP) + DMA1 ch5 | CPU2 | CPU2 vectors `USART2_IRQn`, `DMA1_Channel4_5_6_7_IRQn` |
| Low power | CPU2 | Sleep/WFI only; Stop 2 needs CPU1/CPU2 IPCC+HSEM (not implemented) |

Interrupt handlers in `Core/Src/stm32wlxx_it.c` use CPU2 vector names from
`Core/Startup/startup_stm32wl55xx_cm0plus.s`.

## 3. Build and flash

### 3.1 Toolchain prerequisite

`arm-none-eabi-gcc` **with newlib**, `cmake` ≥ 3.22, `ninja`. The Homebrew
toolchain on macOS lacks newlib and fails at `stdint.h`; use ARM GNU Toolchain
13.3.Rel1 or 15.3.Rel2 instead.

```sh
export PATH=/Applications/ArmGNUToolchain/15.3.rel2/arm-none-eabi/bin:$PATH
arm-none-eabi-gcc -print-file-name=libc.a   # must print absolute path
```

### 3.2 Role-specific builds (from MXproject root)

```sh
# PAYLOAD (Master, sends PING every 5 s)
cmake --preset payload && cmake --build --preset payload

# GROUND (Slave, replies PONG)
cmake --preset ground && cmake --build --preset ground
```

Each preset builds both cores (CPU1 + CPU2) with the selected `FW_ROLE`.
Artifacts (Debug preset):
- `build/payload/CM4/Debug/MXproject_CM4.elf` → 0x08000000
- `build/payload/CM0PLUS/Debug/MXproject_CM0PLUS.elf` → 0x08020000
- `build/ground/CM4/Debug/MXproject_CM4.elf` → 0x08000000
- `build/ground/CM0PLUS/Debug/MXproject_CM0PLUS.elf` → 0x08020000

**Both images must be flashed**: CPU1 releases CPU2, CPU2 runs the radio.

## 4. Limitations

- **No GNSS, no sensors (MPU/BNO/BME)**: This build is LoRa-only PingPong.
  Former GNSS UART bridge and SRAM1 mailbox have been removed.
- **Static compile-time role only**: No runtime self-arbitration, no pin-strap.
- **No low-power Stop 2**: CPU2 uses WFI sleep only.
- **Single-band**: Radio front-end is 470 MHz (NUCLEO-WL55JC2). 868/915 MHz
  would require different hardware.
- **Hardware untested**: Only compilation, linking and CPU2 vector-table wiring
  verified on host. Bench validation pending (see section 5).

## 5. Hardware validation required

1. Flash both cores on two NUCLEO-WL55JC2 boards (one PAYLOAD, one GROUND).
2. Confirm CPU2 banner appears on VCP of both boards.
3. Verify Master logs `OnTxDone`/`PING` and Slave logs `OnRxDone`/`PONG`;
   both boards toggle LED2 (GREEN) and LED3 (RED) periodically via `OnledEvent` timer —
   radio IRQ → sequencer task → TX/RX cycles work.
4. Check RSSI/SNR plausibility and 3 s RX timeout / re-sync path.
5. Confirm CPU1 is not disturbed by CPU2 interrupts.

## 6. Regenerating from MXproject.ioc

- `CM0PLUS/CMakeLists.txt`: re-add `include("subghz_phy.cmake")` if CubeMX rewrites.
- `MXproject/mx-generated.cmake`: re-apply the `MX_BUILD_TYPE_DIR` block (per-build-type
  `BINARY_DIR` override) so Debug/Release presets do not share sub-builds.
- `.ioc` lists SUBGHZ and SubGHz_Phy for CM0Plus; USART2 is assigned to CM4 (not CM0Plus);
  RTC is not an explicit IP but LSE (PC14/PC15) is configured.
  CubeMX regeneration will not recreate hand-adapted sources — they survive as
  long as "delete previous generated files" does not remove unlisted files.