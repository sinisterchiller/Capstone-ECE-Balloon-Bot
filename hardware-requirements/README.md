# ECE Balloon Bot — Component Selection Audit

**Revision:** 2.0  
**Date:** 21 October 2026  
**Scope:** Component choices only: exact revised parts, comparisons with the earlier selections, manufacturer operating specifications, and selection-specific limitations. Project requirements, system architecture, firmware, thermal design, flight procedures, and test plans are documented in the separate project README.

## 1. Selection summary

**Decision:** Retain the eight revised parts as the preferred **prototype component selections**. The current selection assumes the LoRa antenna and electronic modules are mounted in a **heated enclosure around −30 °C**, while the externally exposed temperature-sensing thermistor must tolerate **approximately −60 °C**. These are planned operating conditions, not a claim that the assembled hardware has already passed environmental testing.

| Function | Earlier selection | Revised selection | Selection assessment |
|---|---|---|---|
| LoRa transceiver | Adafruit RFM95W breakout (SX1276) | **RF Solutions LAMBDA62C-9S** (SX1262) | **Upgrade** — lower specified receive current, +22 dBm maximum TX; harder board integration |
| LoRa antenna | TE Linx ANT-916-CW-RCS-SMA + SMA connector | **Abracon AEACAQ190012-S915** | **Trade-off in favor of cold rating and connection** — −40 °C vs −20 °C, but 3.5 dBi vs 4.8 dBi specified peak gain |
| Humidity / temperature | Adafruit BME280 | **Adafruit 5665 / SHT45** | **Upgrade for humidity accuracy**; pressure and low-temperature air sensing now separate |
| GNSS | Bare u-blox SAM-M10Q-00B | **SparkFun 21834 / SAM-M10Q breakout** | **Prototyping upgrade**, not a better GNSS receiver |
| Accelerometer / gyro | Generic MPU-6050 module | **Adafruit 4692 / LSM6DSO32** | **Integration and feature upgrade**, with a motion-resolution trade-off |
| Onboard microSD interface | Not yet selected | **Adafruit 4682 SPI/SDIO breakout** | **Selection completed**; card is separate |
| External air temperature | BME280's internal temperature sensor | **Littelfuse PT103J2 thermistor** | **Major temperature-range improvement**: rated down to −80 °C |
| Atmospheric pressure | BME280 | **MIKROE-4903 / MS5611** | **Major pressure-range improvement**: specified measurement range includes pressure near 12 km |

> **Selection-status meaning:** “Upgrade” refers to the particular specifications or integration advantages identified below. It does **not** mean the revised part is universally better or that the complete system is flight-qualified.

## 2. Detailed choices and comparison

### 2.1 LoRa transceiver — RF Solutions LAMBDA62C-9S

**Revised part:** [LAMBDA62C-9S — DigiKey](https://www.digikey.ca/en/products/detail/rf-solutions/LAMBDA62C-9S/9749699) · [LAMBDA62 manufacturer datasheet (PDF)](https://mm.digikey.com/Volume0/opasdata/d220001/medias/docus/5952/LAMBDA62.pdf)  
**Earlier part:** [Adafruit RFM95W / product 3072 — DigiKey](https://www.digikey.ca/en/products/detail/adafruit-industries-llc/3072/6005357)

| Property | Earlier RFM95W | Revised LAMBDA62C-9S |
|---|---|---|
| Radio core | SX1276 family | SX1262 |
| Maximum TX output | +20 dBm | +22 dBm |
| Receive current | ~30 mA cited for original breakout | ~4.6 mA typical in LoRa RX |
| TX current | Depends on TX setting | ~118 mA typical at +22 dBm |
| Supply / logic | 3.3 V radio | 1.8–3.7 V module; 3.3 V nominal |
| Temperature range | Verify exact breakout/module rating | −40 to +85 °C |
| Host link | SPI | SPI plus control signals |
| Antenna interface | Solderable RF antenna connection / connector option | Integrated U.FL receptacle |

**Why revised is preferable:** Lower receive-mode current is valuable for two-way LoRa communication, including receive windows for acknowledgements; +2 dB maximum available transmit output may improve link budget where legal. The matched RF module with U.FL makes attachment of a compatible coax antenna easier.

**Trade-offs / constraints:** This is a **castellated surface-mount radio module**, not a header-ready breakout. Use a small carrier/adapter for robust breadboard connections; soldering directly onto unsupported pads is less mechanically reliable. The STM32 needs the radio's SPI signals, `BUSY`, `DIO1`, reset, and `TX_SWITCH` / `RX_SWITCH` control. The manufacturer documents **LDO** regulator configuration for the module. Its documented certified configuration is centered near **918 MHz**, with antenna-specific approval conditions; the selected Abracon antenna is **not automatically covered** by that modular approval. Manufacturer sensitivity settings and peak power do not guarantee a 100 km link.

**Selection verdict:** **Keep**, subject to carrier integration and confirmation of the authorized frequency/antenna combination.

### 2.2 LoRa antenna — Abracon AEACAQ190012-S915

**Revised part:** [AEACAQ190012-S915 — DigiKey](https://www.digikey.ca/en/products/detail/abracon-llc/AEACAQ190012-S915/8593253) · [Abracon manufacturer datasheet (PDF)](https://abracon.com/datasheets/AEACAQ190012-S915.pdf)  
**Earlier part:** [TE ANT-916-CW-RCS-SMA — DigiKey](https://www.digikey.ca/en/products/detail/te-connectivity-linx/ANT-916-CW-RCS-SMA/16630288) · [TE CW-RCS datasheet (PDF)](https://www.te.com/commerce/DocumentDelivery/DDEController?Action=srchrtrv&DocNm=CW-RCS_Series&DocType=Data+Sheet&DocLang=English&DocFormat=pdf&PartCntxt=ANT-916-CW-RCS-SMA) · previously required [CON-SMA-EDGE-S connector](https://www.digikey.ca/en/products/detail/rf-solutions/CON-SMA-EDGE-S/5845767)

| Property | Earlier TE RCS | Revised Abracon |
|---|---|---|
| Nominal band | 902–930 MHz | 915 MHz ±8 MHz (nominal 907–923 MHz) |
| Specified peak gain | 4.8 dBi | 3.5 dBi |
| Minimum rated temperature | −20 °C | **−40 °C** |
| Impedance | 50 Ω | 50 Ω |
| Specified VSWR | ≤2.0 | ≤2.5 per manufacturer datasheet |
| RF termination | SMA male; separate RF connector/cable implementation | 100 mm coax with IPEX-style connector; verify exact MHF1 mating type |
| Physical size | Right-angle whip | ~190 mm antenna length |

**Why revised is preferable:** Extends the specified minimum temperature by **20 °C** and provides an integrated coax/connector arrangement that can mate to the LAMBDA62C U.FL interface when connector variants match. This avoids a separate antenna SMA edge connector.

**Trade-offs / constraints:** The original TE antenna has **higher specified peak gain** (4.8 vs 3.5 dBi); the new antenna is **not a pure RF-gain upgrade**. Installed gain depends on orientation, nearby conductors, mounting and enclosure materials. The plan is to place the Abracon **inside the heated approximately −30 °C enclosure**, because its −40 °C rating does **not** cover direct −60 °C exposure. Its **190 mm length** is difficult to fit inside an enclosure about **200 mm externally** once insulation thickness and clearance are considered. Avoid conductive/foil insulation adjacent to the antenna and provide coax strain relief. The different 3.5 dBi antenna is not established as covered by RF Solutions' original modular approval.

**Selection verdict:** **Keep as the preferred prototype antenna**, conditional on internal fit, installed RF performance, and regulatory compatibility.

### 2.3 Humidity and local temperature — Adafruit 5665 / Sensirion SHT45

**Revised part:** [Adafruit 5665 — DigiKey](https://www.digikey.ca/en/products/detail/adafruit-industries-llc/5665/17831054) · [Supplied Adafruit documentation (PDF)](https://mm.digikey.com/Volume0/opasdata/d220001/medias/docus/5009/DS_1528_5665_Web.pdf) · [Sensirion SHT45 product information](https://sensirion.com/products/catalog/SHT45)  
**Earlier part:** [Adafruit BME280 / product 2652 — DigiKey](https://www.digikey.ca/en/products/detail/adafruit-industries-llc/2652/5604372) · [Bosch BME280 datasheet (PDF)](https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bme280-ds002.pdf)

| Property | Earlier BME280 | Revised SHT45 |
|---|---|---|
| Typical humidity accuracy | ±3% RH | ±1% RH under specified conditions |
| Typical temperature accuracy | ~±0.5 °C near 25 °C | ~±0.1 °C under specified conditions |
| Operating temperature | −40 to +85 °C | −40 to +125 °C |
| Relative-humidity range | 0–100% RH | 0–100% RH |
| Pressure measurement | Yes | **No** |
| Data interface | I²C or SPI | I²C |
| Breadboard integration | Breakout | Breakout with header pads / STEMMA QT |

**Why revised is preferable:** Better specified **typical** humidity and temperature accuracy and a dedicated RH sensor independent from external ambient-temperature and pressure sensors.

**Trade-offs / constraints:** The SHT45 does **not** replace BME280 pressure sensing; that role goes to the MS5611. Its published accuracy values are not promises at every operating temperature. Although it tolerates **−30 °C electronics temperatures**, it is **not rated for direct −60 °C ambient exposure**. An SHT45 kept inside the heated enclosure measures that environment's humidity, **not directly the outside relative humidity**. High-altitude RH is currently **deprioritized**, rather than treated as solved.

**Selection verdict:** **Keep** for prototype and temperature-qualified humidity readings.

### 2.4 GNSS — SparkFun 21834 / u-blox SAM-M10Q breakout

**Revised part:** [SparkFun 21834 — DigiKey](https://www.digikey.ca/en/products/detail/sparkfun-electronics/21834/18735208) · [u-blox SAM-M10Q manufacturer datasheet (PDF)](https://docs.sparkfun.com/SparkFun_u-blox_SAM-M10Q/assets/component_documentation/SAM-M10Q_DataSheet_UBX-22013293.pdf) · [SparkFun breakout hardware guide](https://docs.sparkfun.com/SparkFun_u-blox_SAM-M10Q/hardware_overview/)  
**Earlier part:** [Bare u-blox SAM-M10Q-00B — DigiKey](https://www.digikey.ca/en/products/detail/u-blox/SAM-M10Q-00B/16672678)

| Property | Earlier bare SAM-M10Q | Revised SparkFun breakout |
|---|---|---|
| GNSS chip / antenna | SAM-M10Q with integrated antenna | **Same** SAM-M10Q with integrated antenna |
| Published GNSS altitude limit | 80,000 m | Same underlying limit |
| Published GNSS operating temperature | −40 to +85 °C | Same underlying IC limit; whole board not independently qualified by this |
| Connections | Surface-mount pads | Qwiic and 0.1-inch header access; UART/I²C |
| Supporting circuitry | Depends on your PCB | Ground plane, backup supply and breakout infrastructure |

**Why revised is preferable:** Much easier to prototype and debug without a custom GNSS PCB. The receiver's inherent GNSS performance and altitude specification are **not improved**; the choice improves integration convenience.

**Trade-offs / constraints:** More expensive, larger, and with additional components that may have their own temperature limits (especially the backup battery). Install with suitable view of the sky through nonmetallic material.

**Selection verdict:** **Keep for module-based prototyping**.

### 2.5 IMU — Adafruit 4692 / LSM6DSO32

**Revised part:** [Adafruit 4692 — DigiKey](https://www.digikey.ca/en/products/detail/adafruit-industries-llc/4692/13250946) · [Adafruit LSM6DSO/ISM330 family guide (PDF)](https://cdn-learn.adafruit.com/downloads/pdf/lsm6dsox-and-ism330dhc-6-dof-imu.pdf)  
**Earlier part:** [Generic MPU-6050 module — prior Amazon listing](https://www.amazon.ca/dp/B07V67DQ5N)

| Property | Earlier MPU-6050 | Revised LSM6DSO32 |
|---|---|---|
| Accelerometer selectable ranges | ±2, ±4, ±8, ±16 g | ±4, ±8, ±16, ±32 g |
| Gyroscope range | Up to ±2000°/s | Up to ±2000°/s |
| Silicon operating temperature | −40 to +85 °C | −40 to +85 °C |
| Interface | I²C | I²C or SPI |
| Board support | Generic module-specific | Documented Adafruit breakout and software examples |

**Why revised is preferable:** More transparent component traceability, established breakout documentation, optional SPI interface and a wider maximum accelerometer range for unusual shocks.

**Trade-offs / constraints:** The LSM6DSO32's lowest selectable ±4 g range is higher than the MPU-6050's ±2 g range, so it does **not automatically offer better sensitivity to small balloon oscillations**. The ±32 g rating is unnecessary for ordinary ascent but useful for high-acceleration events.

**Selection verdict:** **Keep**, choosing the acceleration range based on actual motion signals.

### 2.6 Nonvolatile storage interface — Adafruit 4682

**Revised part:** [Adafruit 4682 — DigiKey](https://www.digikey.ca/en/products/detail/adafruit-industries-llc/4682/12822319) · [Adafruit microSD SPI/SDIO guide (PDF)](https://cdn-learn.adafruit.com/downloads/pdf/adafruit-microsd-spi-sdio.pdf)  
**Earlier selection:** MicroSD storage required, **no specific module selected**.

| Property | Revised specification |
|---|---|
| Interfaces | SPI and SDIO |
| Supply | **3.3 V only** |
| Logic level | **3.3 V only** |
| Physical format | Breakout with solderable header holes |
| MicroSD card included? | **No** |
| Complete breakout + card temperature qualification | **Not established by the interface selection alone** |

**Why revised is preferable:** It converts an outstanding component choice into a documented, easily connected storage interface while retaining SPI and SDIO options.

**Trade-offs / constraints:** The 4682 is **not 5 V tolerant**; storage survival and reliability also depend on the chosen microSD card, not merely this interface board. A temperature-qualified card must be selected separately.

**Selection verdict:** **Keep**.

### 2.7 Exposed atmospheric temperature — Littelfuse PT103J2

**Revised part:** [PT103J2 — DigiKey](https://www.digikey.ca/en/products/detail/littelfuse-ibu/PT103J2/1014538) · [Littelfuse PT thermistor series datasheet (PDF)](https://www.littelfuse.com/assetdocs/littelfuse-leaded-thermistors-standard-precision-pt-datasheet?assetguid=9edb82b2-89bd-4657-aa6b-3005ae32e88b)  
**Earlier sensing:** [BME280 — DigiKey](https://www.digikey.ca/en/products/detail/adafruit-industries-llc/2652/5604372) used for atmospheric temperature.

| Property | Earlier BME280 | Revised PT103J2 |
|---|---|---|
| Temperature rating | −40 to +85 °C | **−80 to +150 °C** |
| Measurement element | Digital temperature IC | Passive NTC thermistor bead |
| Signal | I²C / SPI | Resistance read through divider / ADC |
| Nominal resistance | N/A | **10 kΩ at 25 °C** |
| Beta coefficient | N/A | **3892 K (0–50 °C reference interval)** |
| Quoted interchangeability | Different IC error specification | **±0.2 °C only over 0–70 °C** |

**Why revised is preferable:** The thermistor's rated range **includes −60 °C**, whereas the BME280's does not. Its small remote sensing bead can be placed **outside** the heated enclosure while sensitive electronics stay inside.

**Trade-offs / constraints:** Not a plug-and-play digital sensor: it needs a suitable resistor divider/ADC and a resistance–temperature conversion curve. Resistance rises markedly at low temperature; **accuracy at −60 °C must not be inferred from the ±0.2 °C specification for 0–70 °C**.

**Selection verdict:** **Keep**. An additional thermistor for internal temperature monitoring is optional rather than a change to the chosen part.

### 2.8 Atmospheric pressure — MIKROE-4903 / TE MS5611

**Revised part:** [MIKROE-4903 Altitude 6 Click — DigiKey](https://www.digikey.ca/en/products/detail/mikroelektronika/MIKROE-4903/16182509) · [TE MS5611 manufacturer datasheet (PDF)](https://www.te.com/commerce/DocumentDelivery/DDEController?Action=showdoc&DocId=Data+Sheet%7FMS5611-01BA03%7FB3%7Fpdf%7FEnglish%7FENG_DS_MS5611-01BA03_B3.pdf%7FMS561101BA03-50) · [MikroElektronika board page](https://www.mikroe.com/altitude-6-click)  
**Earlier part:** [Adafruit BME280 — DigiKey](https://www.digikey.ca/en/products/detail/adafruit-industries-llc/2652/5604372)

| Property | Earlier BME280 | Revised MS5611 |
|---|---|---|
| Specified pressure range | 300–1100 hPa | **10–1200 mbar extended range** |
| Range containing ~193 hPa (12 km standard atmosphere)? | **No** | **Yes** |
| Region of detailed / full-accuracy specifications | BME280's own pressure specification within normal range | **450–1100 mbar** |
| Operating temperature | −40 to +85 °C | −40 to +85 °C |
| Digital interface | I²C / SPI | I²C or SPI on Click board; I²C default |
| Breakout supply/logic | Depends on breakout | **3.3 V** |

**Why revised is preferable:** The **BME280 is below its specified pressure limit** by 12 km (standard atmospheric pressure ~193 hPa), while the **MS5611 is still within its 10–1200 mbar extended measurement range**. The Click board supplies an accessible module format.

**Trade-offs / constraints:** The MS5611's **full-accuracy range begins at 450 mbar**; measuring 193 hPa is not the same as having guaranteed high precision there. Temperature protection does not remove the need for the pressure sensor to be **pressure-equalized with outside air**; the device cannot infer ambient pressure from a sealed heated chamber. Interface selection on the Click board uses jumpers.

**Selection verdict:** **Keep as the pressure-sensing prototype choice**, conditional on low-pressure accuracy characterization.

## 3. Component operating-condition summary

**Important:** Temperature ranges below are the applicable IC/module/antenna ratings stated in the linked specifications. They do **not automatically qualify an entire breakout**, including backup cells, connectors, adhesives, or the microSD card.

| Revised component | Key operating conditions | Planned installation | Selection-specific limitation |
|---|---|---|---|
| LAMBDA62C-9S | 1.8–3.7 V; **−40 to +85 °C**; up to +22 dBm TX | Heated enclosure ~−30 °C | Carrier needed; specific certified radio/antenna operating conditions |
| Abracon AEACAQ190012-S915 | 915 ±8 MHz; **−40 to +85 °C**; 3.5 dBi peak; 50 Ω | **Inside heated enclosure** | 190 mm fit, installation detuning, antenna approval |
| SHT45 | **−40 to +125 °C**; 0–100% RH; I²C | Heated enclosure for prototype measurement | Not qualified to sense −60 °C exterior RH directly |
| SparkFun SAM-M10Q | Receiver **−40 to +85 °C**; GNSS altitude limit 80 km | Heated enclosure, antenna sky-facing | Backup cell/whole-board low-temp qualification not verified |
| LSM6DSO32 | **−40 to +85 °C**; accel ±4 to ±32 g | Heated enclosure | Range selection and low-motion noise trade-off |
| Adafruit microSD 4682 | **3.3 V supply and I/O**, SPI/SDIO | Heated enclosure | Full board and separately purchased card temperature rating not established |
| PT103J2 | **−80 to +150 °C**; 10 kΩ @25 °C | **Outside**, exposed to ambient air | Cold-end conversion accuracy / ADC design |
| MIKROE-4903 / MS5611 | **−40 to +85 °C**; 10–1200 mbar extended pressure range | Warm but exposed to exterior static pressure | Accuracy outside 450–1100 mbar full-accuracy interval |

## 4. Final component-selection decisions

| Revised part | Decision | What prevents treating its selection as unconditionally final |
|---|---|---|
| **LAMBDA62C-9S** | **Keep** | Regulatory frequency/antenna configuration; mechanical carrier |
| **AEACAQ190012-S915** | **Keep, conditional** | Physically fitting inside warm enclosure, realized RF performance, certification compatibility |
| **Adafruit SHT45** | **Keep** | External −60 °C RH is not within this sensor's specification (currently not a priority) |
| **SparkFun SAM-M10Q** | **Keep** | Whole-breakout low-temperature qualification, particularly backup cell |
| **Adafruit LSM6DSO32** | **Keep** | No major component-selection blocker |
| **Adafruit microSD 4682** | **Keep** | Compatible rated microSD card still to select |
| **Littelfuse PT103J2** | **Keep** | Temperature calibration/uncertainty at −60 °C |
| **MIKROE-4903 / MS5611** | **Keep, conditional** | Accuracy around 193 hPa is not covered by quoted full-accuracy range |

**Conclusion:** The eight revised choices are defensible for prototype component selection. The meaningful improvements over the previous list are lower receive-mode radio current, improved typical RH accuracy, a dedicated external thermistor rated for −60 °C conditions, a pressure sensor whose range extends to the target high altitude, and ready-made GNSS/IMU/storage boards. The Abracon antenna trades some published peak gain for a colder temperature rating and a more convenient coax/U.FL-style connection. **Keeping it inside the heated enclosure makes the temperature range plausible, but the installed antenna and its radio certification still require confirmation.**
