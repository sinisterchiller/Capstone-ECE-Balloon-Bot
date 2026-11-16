# Implementation Plan — STM32WL55JC2 LoRa Transceiver

Goal: Implement bidirectional LoRa communication between two STM32WL55 boards using official STMicroelectronics drivers, without LoRaWAN, third-party radio libraries, or unnecessary complexity.

Recommended approach: Use ST's `STM32CubeWL` firmware package, specifically its `SubGHz_Phy` middleware and `SubGHz_Phy_PingPong` example. This provides the radio initialization, transmission, reception, and interrupt handling needed for raw LoRa communication.&#x20;

[image](https://www.google.com/s2/favicons?domain=https://www.st.com\&sz=32)

Product - STMicroelectronics+1



## Important hardware consideration

The NUCLEO-WL55JC2 is the low-frequency variant, tuned approximately for 470–520 MHz according to ST's AN5406.

| Board          | RF band     |
| -------------- | ----------- |
| NUCLEO-WL55JC1 | 865–930 MHz |
| NUCLEO-WL55JC2 | 470–520 MHz |

The JC2 should not be configured for 915 MHz just by changing the firmware frequency. The antenna and RF matching must also support the selected band.&#x20;

[image](https://www.google.com/s2/favicons?domain=https://www.st.com\&sz=32)

Application note+1



Because you're in Canada, the permitted operating frequency and transmit power need to be verified before transmitting. The plan below leaves frequency selection as an explicit configuration step.

## Phase 1 — Establish the official ST project

1. Obtain the [official STM32CubeWL firmware package](https://github.com/STMicroelectronics/STM32CubeWL?utm_source=chatgpt.com).
2. Use the supplied application:

`Projects/NUCLEO-WL55JC/Applications/SubGHz_Phy/SubGHz_Phy_PingPong/`

3. Select the single-core Cortex-M4 implementation to avoid unnecessary dual-core complexity.
4. Build the unmodified project using the STM32 toolchain in VS Code.
5. Confirm the board can be flashed and debugged successfully.

Completion: Firmware builds and runs without radio configuration or build errors.

## Phase 2 — Configure the LoRa radio

Modify `SubGHz_Phy/App/subghz_phy_app.h`.

| Parameter               | Initial configuration                        |
| ----------------------- | -------------------------------------------- |
| Modulation              | LoRa                                         |
| `RF_FREQUENCY`          | Select a legal, hardware-supported frequency |
| `LORA_BANDWIDTH`        | `0` = 125 kHz                                |
| `LORA_SPREADING_FACTOR` | `7`                                          |
| `LORA_CODINGRATE`       | `1` = 4/5                                    |
| `LORA_PREAMBLE_LENGTH`  | `8`                                          |
| `TX_OUTPUT_POWER`       | Set within regulatory and board limits       |
| Payload length          | Start with a short fixed message             |

These parameter names and defaults come from ST's official PingPong example. Its default frequency is 868 MHz, so change that before running it on a JC2 board.&#x20;

[image](https://www.google.com/s2/favicons?domain=https://github.com\&sz=32)

GitHub+1



Completion: Both boards have matching, valid LoRa configurations.

## Phase 3 — Implement transmission and reception

Keep ST's provided radio driver and callback mechanism. No custom register-level radio driver is necessary.

Basic bidirectional communication

Use the official `Radio` API:

| Function / callback             | Purpose                              |
| ------------------------------- | ------------------------------------ |
| `Radio.Init()`                  | Initialize radio and event callbacks |
| `Radio.SetChannel()`            | Select RF frequency                  |
| `Radio.SetTxConfig()`           | Configure transmission               |
| `Radio.SetRxConfig()`           | Configure reception                  |
| `Radio.Send()`                  | Transmit payload                     |
| `Radio.Rx()`                    | Enter receive mode                   |
| `OnTxDone()`                    | Handle successful transmission       |
| `OnRxDone()`                    | Read received payload, RSSI and SNR  |
| `OnRxTimeout()` / `OnRxError()` | Handle receive failures              |

These are already present in ST's [official application source](https://github.com/STMicroelectronics/STM32CubeWL/blob/main/Projects/NUCLEO-WL55JC/Applications/SubGHz_Phy/SubGHz_Phy_PingPong/SubGHz_Phy/App/subghz_phy_app.c?utm_source=chatgpt.com).&#x20;

[image](https://www.google.com/s2/favicons?domain=https://github.com\&sz=32)

GitHub+2



Completion: Board A sends `PING`; Board B receives it and responds `PONG`; Board A receives the response.

## Phase 4 — Support application messages

Once PingPong works, replace the demonstration payload with application data.

1. Accept a message through a fixed test buffer or UART.
2. Send the message using `Radio.Send()`.
3. On reception, validate the payload length and extract the message.
4. Print the received message, RSSI and SNR over UART.
5. Return to receive mode after transmission or a timeout.

Keep the existing ST sequencer and callback handling instead of introducing an RTOS or separate communication framework.

Completion: Both boards can send and receive arbitrary short messages, not just `PING` and `PONG`.

## Phase 5 — Verification

- Official ST firmware builds successfully.
- Radio initializes without errors.
- Board A transmits and Board B receives.
- Board B transmits and Board A receives.
- Received payload matches transmitted payload.
- UART reports RSSI/SNR for received packets.
- RX timeouts and errors do not permanently stop communication.
- Multiple consecutive exchanges work reliably.

## Implementation boundaries

Keep the first implementation limited to raw point-to-point LoRa communication. Do not introduce LoRaWAN, gateways, RTOS, encryption, mesh networking, or custom radio drivers.

If the eventual target is a bare STM32WL55 on a custom PCB instead of a Nucleo board, retain ST's radio middleware but adapt `radio_board_if.c` and the BSP for the custom RF switch, TCXO, and hardware connections.

## Official references

[STM32CubeWL — Official ST firmware repository](https://github.com/STMicroelectronics/STM32CubeWL?utm_source=chatgpt.com)[ST SubGHz PHY PingPong — Example instructions](https://github.com/STMicroelectronics/STM32CubeWL/blob/main/Projects/NUCLEO-WL55JC/Applications/SubGHz_Phy/SubGHz_Phy_PingPong/readme.txt?utm_source=chatgpt.com)[AN5406 — How to build a LoRa application with STM32CubeWL](https://www.st.com/resource/en/application_note/dm00660451.pdf?utm_source=chatgpt.com)

Final deliverable: A working, two-way, half-duplex LoRa transceiver using ST's official radio drivers, with application-level messaging and UART diagnostics.