# ⚠️ DECOMMISSIONED — Historical Reference Only

**This directory (`ground-station/stmLora-trial/`) is decommissioned and preserved for historical reference only.**

## Status

- **Superseded by:** [`ground-station/MXproject/`](../MXproject/) — the current STM32WL55JC dual-core (CM4 + CM0+) LoRa-only PingPong bench project at 470 MHz.
- **Commit reference:** MXproject is committed at `936f371` on branch `feature/ground-station`.
- **No production/flight integration:** This trial was a standalone LoRa PING/PONG experiment on NUCLEO-WL55JC2 (470–520 MHz). It has **not** been hardware-validated for flight, does not include GPS/IMU sensors, and was never integrated with the balloon payload or ground station dashboard.
- **No 915 MHz flight validation:** The NUCLEO-WL55JC2 front-end is the 470 MHz band variant; 915 MHz operation would require different hardware and has not been tested.

## What This Trial Contained

- ST SubGHz_Phy middleware PingPong example adapted for NUCLEO-WL55JC2
- Two roles: Initiator (sends PING) / Responder (replies PONG)
- Compile-time role and TX selection via CMake presets (`Debug`, `Release`, `InitiatorTx`, `ResponderTx`)
- UART diagnostics at 115200 8N1 (ST-LINK VCP)
- **Removed in the decommissioning commit (3710a3f):**
  - Startup DIAG UART markers (`DiagMarker` blocking writes on USART2)
  - Periodic RSSI sampling and chip-mode diagnostic logging (~3 s interval)
  - Radio state tracing before/after TX and RX operations
- PING/PONG protocol logic remains intact

## Why Decommissioned

This trial served its purpose as a LoRa link validation step. The project has since moved to the dual-core MXproject implementation which is a **LoRa-only PingPong bench** at 470 MHz:

- CM4 (CPU1): Minimal boot — configures clocks/board, enables LSE for CPU2 RTC, initializes LEDs/buttons, releases CPU2 via `HAL_PWREx_ReleaseCore()`. No periodic sensor work.
- CM0+ (CPU2): Owns SubGHz_Phy radio and RTC; runs compile-time PAYLOAD/GROUND roles exchanging 64-byte PING/PONG frames every 5 s (LoRa, BW 125 kHz, SF7, CR 4/5, 14 dBm).
- No GPS/GNSS, no IMU (MPU/BNO055), no environmental sensors, no telemetry protocol, no ACK/retransmission, no shared-memory mailbox.
- Hardware untested: Only compilation, linking, and CPU2 vector-table wiring verified on host. Bench validation pending.

## Preservation

All source files, build configuration, and documentation (`BUILD.md`, `loraimplement.md`) are preserved **as-is** in this directory. No further development, builds, or flashes are expected from this path.

---

**For current work, see [`ground-station/MXproject/`](../MXproject/).**