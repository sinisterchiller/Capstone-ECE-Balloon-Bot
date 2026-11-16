# ⚠️ DECOMMISSIONED — Historical Reference Only

**This directory (`ground-station/stmLora-trial/`) is decommissioned and preserved for historical reference only.**

## Status

- **Superseded by:** [`ground-station/MXproject/`](../MXproject/) — the current STM32WL55JC dual-core (CM4 + CM0+) PingPong bench project with full MPU/GPS/LoRa telemetry integration.
- **Commit reference:** MXproject is committed at `936f371` on branch `feature/ground-station`.
- **No production/flight integration:** This trial was a standalone LoRa PING/PONG experiment on NUCLEO-WL55JC2 (470–520 MHz). It has **not** been hardware-validated for flight, does not include GPS/IMU sensors, and was never integrated with the balloon payload or ground station dashboard.

## What This Trial Contained

- ST SubGHz_Phy middleware PingPong example adapted for NUCLEO-WL55JC2
- Two roles: Initiator (sends PING) / Responder (replies PONG)
- Compile-time role and TX selection via CMake presets (`Debug`, `Release`, `InitiatorTx`, `ResponderTx`)
- UART diagnostics at 115200 8N1 (ST-LINK VCP)
- **Removed in the final uncommitted changes (now committed as part of decommissioning):**
  - Startup DIAG UART markers (`DiagMarker` blocking writes on USART2)
  - Periodic RSSI sampling and chip-mode diagnostic logging (~3 s interval)
  - Radio state tracing before/after TX and RX operations
- PING/PONG protocol logic remains intact

## Why Decommissioned

This trial served its purpose as a LoRa link validation step. The project has since moved to the dual-core MXproject implementation which integrates:

- CM4 (CPU1): 100 Hz MPU-9250/MPU-6500 polling, GPS (SAM-M10Q) NMEA parsing, SRAM1 seqlock shared memory
- CM0+ (CPU2): LoRa telemetry v3 (26-byte combined GPS+MPU packet), ACK retransmission, ground RX
- Host test suite (driver logic, codec, snapshot consumer) with sanitizer-clean builds

## Preservation

All source files, build configuration, and documentation (`BUILD.md`, `loraimplement.md`) are preserved **as-is** in this directory. No further development, builds, or flashes are expected from this path.

---

**For current work, see [`ground-station/MXproject/`](../MXproject/).**