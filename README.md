# ECE BALLOON BOT

Group 13  
Reuben George (1800163)  
Anurag Koushik (1806274)  
Bilal Zuhdi Hussain (1812585)  
Sahil Dhami (1710339)

ECE 490 - Electrical Engineering Design Project 1  
Client - Prof. Yize Chen (yize9@ualberta.ca)

The design elements of this project and report are entirely the original work of the authors and have not been submitted for credit in any other course except as follows:

List of exceptions (if applicable):  
None

| Name | Contribution (%) | Sections Contribution |
| --- | --- | --- |
| Reuben George | 25% | Quantitative Requirements, Design Constraints |
| Sahil Dhami | 25% | Introduction, High Level Scope of Requirements and Design Constraints |
| Anurag Koushik | 25% | Block Diagram, High Level Scope of Requirements |
| Bilal Zuhdi Hussain | 25% | Appendices, Design Constraints, References |



## Table of Contents

Introduction 4  
Block Diagram 5  
High-Level Scope of Requirements 6  
Quantitative Requirements 7  
Design Constraints 9  
References 13  
Appendices 14  

## Introduction

### Background

Weather balloons have been widely used to collect atmospheric data, including temperature, barometric pressure, and humidity, since their development in 1892 [1]. Early models relied on mechanical instruments that required physical recovery to obtain recorded data, which often proved challenging. The introduction of radiosondes in the 1930s enabled wireless data transmission [1], while later advancements in GPS technology, digital sensors, and embedded systems significantly improved the accuracy and reliability of atmospheric measurements [2]. Modern radiosondes are capable of measuring temperature, humidity, pressure, wind speed, and wind direction, while transmitting the collected data to ground stations [2]. These advancements also enabled the development of compact, lightweight devices capable of collecting, processing, and transmitting data over long distances.

### Motivation

Traditional weather balloons are primarily designed to collect and transmit atmospheric data, with limited capability to independently respond to changing atmospheric conditions. The motivation for this project is to design a more autonomous weather balloon payload that combines traditional atmospheric data collection with autonomous sensing capabilities, allowing the system to independently monitor atmospheric conditions, detect abnormal events, and transmit accurate, real-time information to a ground station. By incorporating these capabilities, the proposed project aims to improve the efficiency, reliability and responsiveness of atmospheric monitoring devices.

### Objectives

The objective of this project is to design and develop a compact, lightweight, and autonomous weather balloon payload capable of collecting and transmitting atmospheric data to a ground station for further processing and analysis. The project consists of two main components: the weather balloon payload and the ground station. The balloon payload will integrate sensors to measure temperature, barometric pressure, humidity, altitude, and GPS position, along with autonomous sensing capabilities to detect abnormal atmospheric conditions and respond accordingly. Additionally, the payload will also incorporate three operating modes: normal mode for data collection, an event detection mode for when certain predetermined atmospheric conditions are detected, and a low power mode to reduce power consumption when the battery level becomes low. A custom PCB will be designed to integrate the sensors, microcontroller, battery, and wireless communication components while minimizing the payload’s size, weight, and power consumption. The ground station will include a dashboard designed to display real time atmospheric measurements, GPS location, and battery status. Finally, both systems will undergo laboratory and flight testing to evaluate measurement accuracy, communication reliability, autonomous functionality, and overall system performance.

## Block Diagram

```text
FLIGHT SIDE: STM32 processes and logs sensor data; selected telemetry is sent over LoRa.

+------------------------------- FLIGHT PAYLOAD --------------------------------+
|                                                                               |
|  MS5611 pressure sensor --------- I2C -----+                                  |
|  SHT45 humidity sensor ---------- I2C -----+                                  |
|  LSM6DSO32 IMU ------------------ I2C -----+                                  |
|  PT103J2 temperature -- divider -- ADC ----+----> [ STM32 flight MCU ]        |
|  SAM-M10Q GNSS ------------------ UART ----+                |                 |
|                                                           |                   |
|                                              +------------+------------+      |
|                                              |                         |      |
|                                          SPI/SDIO                  SPI/GPIO   |
|                                              |                         |      |
|                                       [ MicroSD storage ]    [ LAMBDA62C-9S ] |
|                                       Samples / events /           |          |
|                                       health / timestamps        U.FL         |
|                                                                     |         |
|                                                   [ Abracon 915 MHz antenna ] |
|                                                                               |
|  STM32: filtering, state estimation, event detection, adaptive sampling,      |
|         telemetry prioritization, ACK handling and limited retries.           |
|                                                                               |
|  Battery (TBD) --> protection / regulation --> all flight electronics         |
|          |                                                                    |
|          +--> power monitor (INA219/INA226-class, TBD) -- I2C --> STM32       |
+-------------------------------------------------------------------------------+
                              |                       ^
                         LoRa telemetry           LoRa ACKs
                              v                       |
+------------------------------- GROUND STATION --------------------------------+
|                                                                               |
|  [ 915 MHz antenna ] -- RF -- [ LAMBDA62C-9S ] -- SPI/GPIO -- [ ESP32-S3 ]    |
|                                                                  |            |
|                                                            USB / Wi-Fi        |
|                                                                  v            |
|                    [ Laptop / Python telemetry ingestion ]                    |
|                         |                          |                          |
|                         v                          v                          |
|              [ Ground processing ]        [ Link status / ACKs ]              |
|              Filtering, validation,       Sequence numbers, RSSI, SNR,        |
|              trends, event analysis,      missing packets, ACK timeouts       |
|              atmospheric profiling                                            |
|                         |                                                     |
|                         v                                                     |
|                   [ Web dashboard ]                                           |
|                   Live charts, position, alerts, link health                  |
+-------------------------------------------------------------------------------+

GROUND SIDE: Received telemetry is further analyzed, stored, and visualized.
```

Figure 1: High-level block diagram of the ECE Balloon Bot flight payload and ground station architecture.[3][4][5][6][7][8]

## High-Level Scope of Requirements

The Balloon Bot should function as an autonomous atmospheric sensing platform integrating onboard processing, adaptive sensing, wireless telemetry, local data storage and ground station based analysis. The requirements are prioritized as High (H) or Medium (M) according to their importance.

### 3.1 Functional Requirements

| Requirement | Priority |
| --- | --- |
| Measuring atmospheric temperature, humidity, pressure, GPS position, altitude and payload motion using onboard sensors | H |
| Perform onboard sensor filtering, altitude change, vertical speed estimation and atmospheric event detection without requiring external intervention | H |
| Operate between three different modes: Normal Mode: Collect, record and transmit atmospheric data at predefined intervals during standard atmospheric conditions. Event Mode: Increase measurement frequency and transmission accordingly when a predefined atmospheric event is detected. Low Power Mode: Increased intervals between atmospheric data collection and transmission when battery level falls below a predefined threshold, while maintaining the ability to activate Event Mode when necessary. | H |
| Dynamic adjustment of sensor measurement rates based on atmospheric event detection with predefined thresholds | H |
| Timestamped recording of sensor measurement, detected events and system health onboard on a microSD card | H |
| Transmit telemetry and priority event alerts over LoRa, transmission rate adaptation with atmospheric event detection | H |
| Battery monitoring, thermal, sensor health and communication health, including detection and report of abnormal operating conditions | H |
| Continue autonomous data collection and onboard storage during temporary or permanent loss with the ground station | H |
| Further data processing on ground station, display of trend, event analysis, vertical atmospheric profiling and real time visualization through a dashboard | M |

### 3.2 Non-Functional Requirements

| Requirement | Priority |
| --- | --- |
| Achieve a minimum battery operating duration of 3 hours under the expected sensor and transmission workload | H |
| Maintain reliable operation during transition between Normal, Event, and Low Power modes | H |
| Design the payload to withstand environmental conditions associated with altitudes up to 12km | M |
| Achieve at least 95% of telemetry packet delivery through the testing phase | M |

## Quantitative Requirements

| Requirement | Target | Verification Method | Justification |
| --- | --- | --- | --- |
| Atmospheric temperature measurement | Measurement error ≤ ±1.0 °C over the tested operating range | Compare against a reference thermometer at multiple temperatures | This accuracy is enough to track temperature changes during the flight without needing expensive sensors. |
| Relative humidity measurement | Measurement error ≤ ±5% over the tested range | Compare against a reference instrument | This allows the system to measure changes in humidity while keeping the sensor cost low. |
| Atmospheric Pressure measurement | Measurement error ≤ ±0.5% relative to the reference pressure | Compare against a calibrated reference pressure sensor | This accuracy is enough to track changes in air pressure as the balloon moves to different altitudes |
| Atmospheric event detection | Detect 100% of predefined test cases and produce no more than one false alert per hour during a specified test | Run a documented set of simulated and recorded sensor scenarios | The system needs to detect important atmospheric changes without giving too many false alerts, which could waste battery power. |
| Adaptive sensor sampling | Increase the sampling rate within 30 seconds of an event and return to the nominal rate after the defined recovery condition | Simulate events and record sampling timestamps | This gives the system enough time to recognize an event and start collecting data more often so important changes are not missed. |
| Battery monitoring | Report battery voltage at intervals of 10 minutes or less and flag a low battery condition at a defined voltage threshold | Compare reported voltage with a multimeter and test the warning threshold | Checking the battery every 10 minutes allows the system to keep track of its power level without checking more often than necessary. |
| Data integrity | Every stored record shall include a timestamp and the required sensor fields | Inspect data files and record incomplete data | Each measurement needs a timestamp and sensor readings so the data can be properly understood and analyzed after the flight. |
| Normal Mode | Sampling Interval of 5 seconds | Inspect recorded timestamps | Taking measurements every 5 seconds provides regular updates while saving battery power and storage space. |
| Event Mode | Sampling Interval of 1 second | Trigger simulated events and inspect timestamps | Taking measurements every second allows the system to collect more detailed data when atmospheric conditions change quickly. |
| Low Power Mode | Sampling Interval of 20 seconds | Simulate low battery conditions and inspect timestamps | Taking measurements less often helps save battery power while still allowing the system to monitor atmospheric conditions. |
| GNSS Tracking | Record GNSS position and altitude every 1 second throughout the flight whenever a valid 3D position fix is available, while transmitting the data at intervals defined by the active operating mode. | Verify GNSS fix status and compare recorded position and altitude against reference measurements during outdoor testing. | Recording the balloon's location and altitude every second makes it easier to see where atmospheric changes happen during the flight. |

## Design Constraints

| Constraint | Justification |
| --- | --- |
| Total payload mass below 500g and dimensions of 20x20x20 cm including PCB, battery, enclosure, antenna, and supporting components (Subject to final design) | To keep the payload compact, lightweight, easy to transport, launch and recover. |
| Total project budget under $1500 | To ensure the project remains financially feasible according to the course and client provided budget estimates while having sufficient funding for components, PCB fabrication, prototyping, balloon construction and related expenses and testing. |
| The final payload must use a custom PCB to integrate the required electronics | To integrate the electronic components into a compact and organized board, reducing unnecessary wiring, weight and risk of loose connections during flight. |
| Suitable battery selection, insulation and thermal management must be used to provide stable power for the expected temperature range and required duration. | To ensure the payload receives sufficient stable power throughout the flight and recovery, particularly at low temperatures where battery performance can decrease. |
| Pre-flight safety checks must be conducted to verify payload condition before flight. | To identify potential electrical, mechanical, and communication failures before launch, reducing the risk to people, property and the payload during failure in flight. |
| The balloon payload must incorporate a suitable parachute and support post flight tracking. A retrieval plan must be established and implemented whenever safely feasibly | To reduce the payload’s descent speed, minimizing the risk of damage or injury to people and property, and improve the chances of recovering the payload and stored data after flight. |
| The payload enclosure, PCB mounting, wiring, antenna connections, and sensor attachments must withstand handling, transport, launch, and descent without significant component displacement or electrical disconnection. | To prevent damage to payload by avoiding loose connections or component displacement during handling, transportation, flight, descent and landing. |
| The payload should be modular and not rigid, to allow components to be replaced or repaired without requiring complete disassembly of the payload for ease of testing. | To simplify component replacement, troubleshooting, and repairs without requiring to disassemble the payload every time (Reducing testing time and maintenance complexity). |
| The antenna must be attached to the payload in such a manner that the PCB, battery, and other conductive components do not impact the signal integrity of the communication setup. | To minimize interference and resulting signal degradation caused by nearby conductive components, improving the reliability and range of wireless communication. |
| Balloon operations must comply with applicable Canadian Aviation Regulations, including CAR 602.42 for large unoccupied free balloons and CAR 601.24 and Standard 621 where tethered objects constitute obstacles to air navigation. | To ensure balloon operations follow applicable aviation legal and safety requirements in order to reduce risk to other aircrafts, property and people. |
| The selected radio module and operating frequency must comply with the Canadian Table of Frequency Allocation for public use, and the transmit power, antenna, and duty cycle shall comply with the Radiocommunication Regulations (SOR/96-484) legislation. | To ensure wireless communication operates within the permitted Canadian frequency bands and applicable limits, preventing interference from other frequencies. |
| Hardware components like the sensors, antenna and others must account for temperature range of -60oC to 40oC for high altitude operation. This must be achieved through suitable component selection, insulation and thermal management. | To account for temperatures that the payload will encounter during high-altitude operation and maintaining reliable data collection, communication throughout operation. |
| Pressure module must be capable of measuring pressures in the range of 10-110 kPa(kiloPascal) for high altitude operation. | To allow atmospheric pressure measurements across a wide range of altitudes, including the lower pressure the payload will encounter at higher altitude. |
| Payload must not contain any enclosed regions of trapped air under low temperature and pressure at 12km altitude | To prevent deformation or damage to the balloon resulting from pressure differences between the interior of the payload and the surrounding atmosphere. |

<u>Trade off analysis of notable components:</u>

Potential options include STM32, ATtiny/meg, raspberry Pi, custom IC/ASIC(Application specific integrated circuit), esp32

Microcontroller(MCU) trade off analysis

Payload-side MCU comparison (scores from 1 to 5; weighted total out of 5):

| Criterion | Weight | STM32 | ATtiny/meg | Raspberry Pi | Custom IC/ASIC | ESP32 |
| --- | --- | --- | --- | --- | --- | --- |
| Cost | 0.1 | 5 | 5 | 5 | 1 | 5 |
| Mass | 0.2 | 5 | 5 | 4 | 5 | 4 |
| Min. Temperature | 0.2 | 3 (−40 °C) | 3 (−40 °C) | 3 (−40 °C) | 4 (custom) | 3 |
| Power efficiency | 0.3 | 5 | 5 | 1 | 5 | 2 |
| Ease of use | 0.2 | 4 | 2 | 5 | 1 | 5 |
| **Weighted total** | **1.0** | **4.4** | **4.0** | **3.2** | **3.6** | **3.5** |

Ground-station comparative criterion scores were not recorded in the original table, so ground-side weighted totals are not reported here.

The raspberry Pi and ESP 32 systems may result in some additional weight in the 10’s of grams thus receiving a mass rating of 4.
Custom ASIC’s cannot be obtained free of charge from the lab.
Ultimately, we choose the STM32 for the payload and the ESP32 for the ground station.

For the temperature sensors, potential options include PT103J2, PS103J2, KT103J2

Temperature sensor trade off analysis

| (1-5 range) | Weight | PT103J2 | PS103J2 | KT103J2 |
| --- | --- | --- | --- | --- |
| Cost | 0.4 | 5($2.824) | 4($2.904) | 3($3.804) |
| Min. Temperature | 0.6 | 5(-80 C) | 5(-80 C) | 5(-80 C) |
| Total | - | 5 | 4.6 | 4.2 |

The temperature sensor chosen will therefore be the PT103J2 sensor.[6]

Pressure sensor trade off analysis

| (1-5 range) | Weight | BME280 | MS561101BA03-50 | 325540009-00 |
| --- | --- | --- | --- | --- |
| Cost | 0.2 | 5($4.35) | 3($19.56) | 1($50) |
| Min. Temperature | 0.3 | 3(-40 C) | 3(-40 C) | 3(-40 C) |
| Min. Pressure | 0.5 | 3(4.35psi) | 5(0.15psi) | 5(0.15psi) |
| Total | - | 3.4 | 4 | 3.6 |

None of the 3 viable pressure sensors considered met the viable operation range, so insulation will be required.
As seen above, the best pressure sensor would be the MS561101BA03.[7]
Cold air has a lower water-vapour capacity, but relative humidity is not necessarily negligible at −40 °C. The selected humidity sensor is now the Adafruit SHT45, which offers better typical humidity accuracy than the BME280 [8]. External atmospheric temperature is measured by the PT103J2 and pressure by the MS5611. The SHT45 is not rated to measure ambient humidity at −60 °C; high-altitude humidity measurements are currently deprioritized.

## References

[1] E. Stover, "The History of Weather Balloons," Sciencing, Mar. 24, 2022. [Online]. Available: https://www.sciencing.com/history-weather-balloons-5050060/. [Accessed: Oct. 11, 2026].

[2] Vaisala, "Radiosonde RS41-SGP," Datasheet, Doc. B211444EN-J. [Online]. Available: https://docs.vaisala.com/v/u/B211444EN-J/en-US. [Accessed: Oct. 11, 2026].

[3] u-blox AG, "SAM-M10Q: u-blox M10 Standard Precision GNSS Antenna Module," Data Sheet, Rev. R05, Apr. 8, 2024. [Online]. Available: https://content.u-blox.com/sites/default/files/documents/SAM-M10Q_DataSheet_UBX-22013293.pdf. [Accessed: Oct. 15, 2026].

[4] STMicroelectronics, "LSM6DSO32: iNEMO Inertial Module with Embedded AI," Data Sheet. [Online]. Available: https://www.st.com/resource/en/datasheet/lsm6dso32.pdf.

[5] RF Solutions, "LAMBDA62: LoRa RF Module," Data Sheet. [Online]. Available: https://mm.digikey.com/Volume0/opasdata/d220001/medias/docus/5952/LAMBDA62.pdf.

[6] Littelfuse, Inc., "PT Series Standard Precision Interchangeable Thermistors," Data Sheet, Feb. 26, 2018. [Online]. Available: https://www.littelfuse.com/assetdocs/littelfuse-leaded-thermistors-standard-precision-pt-datasheet?assetguid=9edb82b2-89bd-4657-aa6b-3005ae32e88b. [Accessed: Oct. 15, 2026].

[7] TE Connectivity, "MS5611-01BA03: Barometric Pressure Sensor, with Stainless Steel Cap," Data Sheet, Jun. 2017. [Online]. Available: https://www.te.com/commerce/DocumentDelivery/DDEController?Action=srchrtrv&DocNm=MS5611-01BA03&DocType=Data%20Sheet&DocLang=English&DocFormat=pdf&PartCntxt=MS561101BA03-50. [Accessed: Oct. 15, 2026].

[8] Adafruit Industries, "Adafruit SHT45 Humidity and Temperature Sensor Breakout," Documentation. [Online]. Available: https://mm.digikey.com/Volume0/opasdata/d220001/medias/docus/5009/DS_1528_5665_Web.pdf.

## Appendices

<u>Client question response :-</u>

Q. How can we ensure that the balloon is not permanently lost during testing?

ANS: I think this could not be 100% guaranteed, and has experimental flavor. Start with bench tests, ground communication tests, and an approved low-altitude tethered test. Any subsequent free flight should have GPS tracking, onboard data storage, a trajectory prediction and recovery plan, and an appropriate descent system.

Q.How high should the balloon fly, and what communication range is required between the payload and ground station?

ANS:\*\*We shall firstly try 30–100 m above ground\*\*, subject to site and aviation clearance. A separate high-altitude stretch objective could target \*\*10–12 km above sea level\*\*, following a flight-readiness review. Note these are design targets. Communication mode shall depend on the budget and testing of different frequencies and devices.

Q. Will the balloon require motion capabilities?

ANS: No propulsion, steering, or active altitude control should be required.

Q. Are permits or other requirements from Transport Canada (TCCA) necessary? If so, will the associated costs be included in the project budget, or will the university or students have to cover them?

ANS: Canadian Aviation Regulation \*\*602.42\*\* requires authorization for an unoccupied free balloon with gas-carrying capacity exceeding \*\*115 ft³. So we shall be okay. \*\*

Q. What are the approximate dimensions and weight limits for the payload?

ANS: I am thinking some dimension of within 20cm\\\*20cm\\\*20cm, but depending on selection of devices and design. For weight it shall be targeting less than 500g.

Q. Is a custom PCB required, or would a non-PCB final product be acceptable?

ANS: It would be recommended to have self-designed PCB which improves integration, weight, or reliability.

Q. What minimum and maximum temperatures should the payload be designed to withstand?

ANS: I am not sure at this moment, roughly say between -20C to 40C

Q. How sophisticated does the ground station dashboard need to be?

ANS: We may start from the most basic ones like ambient temperature, pressure, relative humidity, GPS position/altitude, and power status.

Q. How should communication health be monitored and reported?

ANS: I would suggest only actively sending data when necessary. For instance, If a sustained temperature or humidity change exceeds a selected threshold, sample every second and flag the event.

Q. Who will provide the lab environment for testing?

ANS: We have some lab space, but we may need to go outdoors to do some field tests. At the initial stage, I think a functional laptop dashboard in the lab is sufficient.

Q. Will the final presentation require a live demonstration of the working model at the university?

ANS: My current idea is to have a live demonstration of the integrated payload and dashboard at the final presentation. It should show sensor readings, radio transmission, and logging.

Q. What criteria will define the project as successful?

ANS: I think capstone would be a good training program, and I would be quite happy as long as you are learning something new and exploring some novel topics! Roughly speaking, the success of several key submodules, such as communication, PCB design, sensing, balloon testing, test flight, and data dashboard would be pretty good.

<u>Client in-person meeting notes:-</u>

Q. Helium is rather expensive and we may have difficulty accommodating helium costs into our budget? Where can we obtain it from at reasonable rates?

ANS: Additional budget may be provided to manage helium costs and Home depot/ university labs should be suitable sources.

Q. How many launches should we ideally do?

ANS: Since helium will be covered, as many as possible, starting with bare weights like rocks

Q. Altitude 30-100m or 10-12km? What are we supposed to aim for while designing?

ANS: Initial testing for 30-100m but design for 10 -12 km in case we make the run for that

Q. What sort of behaviour should the autonomous system be doing in order to classify it as such? Is there anything specific you have in mind?

ANS: The autonomous system can decide what raw data to collect, the sampling rate of different pieces of data and decide what must be sent to the ground dashboard. The system can also go through different modes of operation dependent on the environmental situation, including a hibernation mode. As an example, if anything abnormal is detected, the system should exit the hibernation mode and go back to sampling higher.

Client makes mention of his desire to optimize for distance

Q. If distance is a priority, we may need to implement communication protocols using satellite communication?

ANS: There is no need to use satellite communication protocols. We can stick to LoRa but try to design for long range communication with that, and define or estimate the maximum range.

Q. Cameras/real time- footage required? ANS: No

Build and design the balloon setup step by step, most of which will be conducted in the lab. When preliminary testing begins, initial testing attempts should be done at an altitude of 100-200m. Start to make a rough list of components, including parachute, pcb, etc.
