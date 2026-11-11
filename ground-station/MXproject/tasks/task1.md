# STM32WL55 — Payload & Ground Station Implementation Plan

Goal: Replace the existing LoRa PingPong demonstration with a simple, reliable GPS telemetry link using two STM32WL55 boards.

The payload transmits its most recent GPS status every 10 seconds. The ground station acknowledges each packet. If the acknowledgment is missing, the payload retransmits the same packet.

We'll preserve your current dual-core architecture and ST's official radio drivers, without introducing an RTOS, a new radio framework, or unnecessary abstractions.

## 1. Target architecture

Payload

CM4

GPS reception, NMEA parsing, latest sensor status

CM0+

Telemetry transmission, ACK reception, retries, serial logs

Ground

CM4

Initialize and release CM0+

CM0+

Receive telemetry, send ACK, print received data

LoRa data + acknowledgments

The existing project already has most of the required foundation:

- `CM4/Core/Src/nmea_rx.c` receives GPS data.
- `CM4/Core/Src/nmea_gga.c` decodes GGA sentences.
- `Common/GnssVcp/gnss_vcp_mailbox.h` provides shared communication between cores.
- `CM0PLUS/SubGHz_Phy/App/subghz_phy_app.c` contains the existing LoRa radio event handling.
- `CM0PLUS/Core/Src/gnss_vcp_trace.c` forwards GPS logs to the serial monitor.

The main work is replacing the PingPong application behavior and exposing the GPS values as structured data instead of only printable text.

## 2. Implementation stages

## Stage 1 — CMake Payload / Ground Presets

Create two root-level CMake presets named `payload` and `ground`.

| Preset    | CM4                   | CM0+                  |
| --------- | --------------------- | --------------------- |
| `payload` | GPS processing        | Telemetry transmitter |
| `ground`  | Boot / initialization | Telemetry receiver    |

Use one role setting (`FW_ROLE`) to select the application behavior at compile time. Reuse the existing source files rather than duplicating projects.

Important: The existing root `mx-generated.cmake` shares sub-build directories according to Debug/Release. Update that mechanism so Payload and Ground have separate build directories and the role setting reaches both core builds. Otherwise switching presets may reuse the wrong firmware.

Expected commands:

```
cmake --preset payload
cmake --build --preset payload

cmake --preset ground
cmake --build --preset ground
```

Acceptance: Both presets independently compile their CM4 and CM0+ images. Switching presets never produces stale firmware from the other role.

## Stage 2 — Latest GPS Status and Telemetry Format

Keep the existing USART1 reception and NMEA parser on CM4.

Extend the existing parser to expose the latest validated GPS status as structured data, not just a formatted string.

The telemetry should initially contain:

| Field            | Type                           |
| ---------------- | ------------------------------ |
| Protocol version | `uint8_t`                      |
| Message type     | `uint8_t`                      |
| Sequence number  | `uint16_t`                     |
| GPS fix valid    | Boolean flag                   |
| Latitude         | `int32_t`, degrees × 1,000,000 |
| Longitude        | `int32_t`, degrees × 1,000,000 |
| Altitude         | `int32_t`, centimetres         |
| Satellites used  | `uint8_t`                      |

Add a small, coherent latest-status snapshot to the existing shared-memory area so CM0+ can read GPS data directly. Keep the current mailbox ring for serial logs.

Maintain a single telemetry definition and encoding/decoding implementation under `Common/`. Future sensors should only require new fields and corresponding encoding updates, without redesigning the radio flow.

An invalid or outdated GPS fix must be marked as such instead of transmitting old coordinates as current.

Acceptance: The payload's CM0+ can read its latest GPS status, including the no-fix condition, without interrupting GPS reception or serial logging.

## Stage 3 — Payload Transmission Every 10 Seconds

Replace the existing automatic `PING`/`PONG` behavior in `CM0PLUS/SubGHz_Phy/App/subghz_phy_app.c`.

The payload should:

1. Read the latest GPS status.
2. Create a telemetry packet with a new sequence number.
3. Transmit it every 10 seconds.
4. Switch to receive mode and await an ACK.

Reuse ST's existing `Radio.Send()`, `Radio.Rx()`, radio callbacks, sequencer, and timer utilities.

Use a non-blocking 10-second timer. Do not add `HAL_Delay(10000)`.

Acceptance: Payload transmissions occur every 10 seconds, even if GPS has no fix. Serial logs show the sequence number and GPS status.

## Stage 4 — Ground Reception and ACK

The ground station should remain in LoRa receive mode whenever it is not transmitting an ACK.

When a valid telemetry packet arrives:

1. Validate its message type, version, and length.
2. Extract its sequence number and GPS status.
3. Print its contents to the serial monitor.
4. Immediately transmit an ACK containing the same sequence number.
5. Return to receive mode after ACK transmission completes.

If a duplicate packet arrives, acknowledge it again but do not count it as a new telemetry sample.

Acceptance: Every valid packet receives an ACK. Ground logs show coordinates, fix status, sequence number, RSSI, and SNR where available.

## Stage 5 — ACK Timeout and Retransmission

Add one small ACK/retry state machine to the payload.

Proposed prototype settings:

| Parameter               | Value                          |
| ----------------------- | ------------------------------ |
| New telemetry interval  | 10 seconds                     |
| ACK timeout             | 2 seconds after TX completes   |
| Maximum retransmissions | 2                              |
| Total attempts          | 3                              |
| Retry content           | Exact same packet and sequence |

If the correct ACK arrives, clear the pending transmission.

If the ACK is missing, retransmit the original packet. Do not replace its contents with a newer GPS reading while retrying.

After all three attempts fail, log the packet as unacknowledged and continue with the next scheduled telemetry interval.

Only one packet may be awaiting acknowledgment at a time.

Acceptance: Lost acknowledgments cause retransmissions, matching ACKs stop retries, and the payload continues operating if the ground station is offline.

## 3. Communication sequence

## 4. Serial monitor output

The demonstration should show useful information from both boards, rather than dumping raw packets continuously.

Payload

Serial monitor

[GPS] Fix: YES[GPS] Lat: 53.546124[GPS] Lon: -113.493823[TX] #1 sent[ACK] #1 received[TX] #2 sent[ACK] #2 timeout[RETRY] #2 attempt 2[ACK] #2 received

Ground

Serial monitor

[RX] #1 received[GPS] Fix: YES[GPS] Lat: 53.546124[GPS] Lon: -113.493823[RX] RSSI: -65 dBm[ACK] #1 sent[RX] #2 received[ACK] #2 sent[RX] #2 duplicate[ACK] #2 resent

Illustrative logs; not results from hardware testing.

## 5. Final verification

Hardware acceptance checklist

0/9 passed

Payload and Ground CMake presets build both core images independently

Both boards boot and display the correct role

Payload sends the latest GPS status every 10 seconds

Ground receives, decodes, and acknowledges telemetry

Payload stops retrying after a matching ACK

Missing ACK triggers retransmission of the identical packet

Duplicate packets are acknowledged without duplicate telemetry logging

Payload continues transmitting after ground is disconnected

Both boards produce readable, non-corrupted serial logs

Reset checklist

## 6. Scope restrictions

Keep this implementation limited to the requested prototype:

- No FreeRTOS, RTOS tasks, or custom scheduler.
- No additional radio drivers; continue using ST's SubGHz_Phy.
- No persistent retransmission queue or packet database.
- No dynamic memory allocation for telemetry.
- No changes to the GPS hardware interface or unnecessary rewrites of the NMEA parser.
- No ground-station dashboard or other communication protocols at this stage.

Use the project's existing 470 MHz NUCLEO-WL55JC2 radio configuration for the initial controlled tests, subject to the appropriate frequency authorization. This is separate from the future external LoRa transceiver configuration.

Implementation order: CMake presets → structured GPS status → 10-second telemetry → ground ACK → payload retries → hardware verification.

Complete and verify each stage before advancing. The result should be a straightforward telemetry prototype that can accommodate additional sensors without rebuilding its communication architecture.