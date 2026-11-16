/*
 * SPDX-FileCopyrightText: 2024 ECE Balloon Bot Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Host (no ESP-IDF, no hardware) regression test for the BME68x SPI adapter.
 *
 * It covers two things that cannot be exercised on the target without a sensor:
 *
 *  1. The frame construction in bme68x_spi_frame.c that bme68x_esp.c uses for
 *     every register access: read frames (address + BME68X_SPI_RD_MSK, dummy
 *     bytes) and write frames (address masked with BME68X_SPI_WR_MSK followed by
 *     the vendor-interleaved address/data stream inside one CS-low frame).
 *
 *  2. The Bosch API contract the adapter depends on: run unmodified bme68x.c
 *     against a mock BME688 register file that decodes the frames the same way
 *     the sensor does. This checks that
 *       - init/configuration traffic is accepted (chip id, variant id, calibration),
 *       - a multi-register write really lands all five registers in one frame,
 *       - forced-mode data reads yield Bosch-compensated values in the documented
 *         units (degC, Pa, %RH, Ohm) for both BME68X_USE_FPU and integer builds,
 *       - gas_valid_r / heat_stab_r are decoded from the field status bits,
 *       - a read clears new_data ("not already read-out"), so get_data() returns
 *         BME68X_W_NO_NEW_DATA instead of stale numbers, and
 *       - an unread earlier conversion makes get_data() return that older
 *         measurement immediately - the reason bme68x_esp_measure() waits out the
 *         conversion window before its first read.
 *
 * Build and run with tests/run_bme68x_host_test.sh.
 */

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bme68x.h"
#include "bme68x_spi_frame.h"

static int failures;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);        \
            failures++;                                                   \
        }                                                                 \
    } while (0)

/* ------------------------------------------------------------------------- */
/* Frame construction                                                        */
/* ------------------------------------------------------------------------- */

static void test_read_frame(void)
{
    uint8_t frame[BME68X_SPI_FRAME_MAX_BYTES];
    uint32_t len;

    /* Field block read (the forced-mode path): address + 17 dummy bytes. */
    memset(frame, 0xAA, sizeof(frame));
    len = bme68x_spi_read_frame(BME68X_REG_FIELD0, BME68X_LEN_FIELD, frame, sizeof(frame));
    CHECK(len == BME68X_LEN_FIELD + 1);
    CHECK(frame[0] == (BME68X_REG_FIELD0 | BME68X_SPI_RD_MSK));
    for (uint32_t i = 1; i < len; i++) {
        CHECK(frame[i] == 0); /* MOSI stays idle while the sensor drives MISO */
    }

    /* The Bosch layer already sets the read mask; framing must be idempotent. */
    len = bme68x_spi_read_frame(BME68X_REG_FIELD0 | BME68X_SPI_RD_MSK, BME68X_LEN_FIELD, frame,
                                sizeof(frame));
    CHECK(len == BME68X_LEN_FIELD + 1);
    CHECK(frame[0] == (BME68X_REG_FIELD0 | BME68X_SPI_RD_MSK));

    /* Largest calibration block the API reads (42 bytes) fits comfortably. */
    len = bme68x_spi_read_frame(BME68X_REG_COEFF1, BME68X_LEN_COEFF_ALL, frame, sizeof(frame));
    CHECK(len == BME68X_LEN_COEFF_ALL + 1);

    /* Boundaries: ESP32 CPU-driven SPI cannot move more than 64 bytes per frame. */
    len = bme68x_spi_read_frame(0x00, BME68X_SPI_REG_MAX_BYTES, frame, sizeof(frame));
    CHECK(len == BME68X_SPI_FRAME_MAX_BYTES);
    CHECK(bme68x_spi_read_frame(0x00, BME68X_SPI_FRAME_MAX_BYTES, frame, sizeof(frame)) == 0);
    CHECK(bme68x_spi_read_frame(0x00, 0, frame, sizeof(frame)) == 0);
    CHECK(bme68x_spi_read_frame(0x00, 4, frame, 4) == 0); /* capacity must hold len + 1 */
    CHECK(bme68x_spi_read_frame(0x00, 4, NULL, 32) == 0);
}

static void test_write_frame(void)
{
    uint8_t frame[BME68X_SPI_FRAME_MAX_BYTES];
    uint8_t cap[2] = { 0x5A, 0x00 };
    uint32_t len;

    /* Single register write: status/ctrl_meas byte, as used by bme68x_set_op_mode(). */
    len = bme68x_spi_write_frame(BME68X_REG_CTRL_MEAS, cap, 1, frame, sizeof(frame));
    CHECK(len == 2);
    CHECK(frame[0] == BME68X_REG_CTRL_MEAS);
    CHECK(frame[1] == cap[0]);

    /* Multi-register write as produced by bme68x_set_regs() for bme68x_set_conf():
     * the vendor passes the first address plus {data0, addr1, data1, ...}. */
    const uint8_t interleaved[2 * 5 - 1] = {
        0x0A, 0x72, 0x0B, /* data for 0x71, then (addr 0x72, data 0x0B) */
        0x0C, 0x74, 0x0D, /* (addr 0x73, data 0x0C), (addr 0x74, data 0x0D) */
        0x0E              /* (addr 0x75, data 0x0E) */
    };
    len = bme68x_spi_write_frame(0x71, interleaved, sizeof(interleaved), frame, sizeof(frame));
    CHECK(len == sizeof(interleaved) + 1);
    CHECK(frame[0] == 0x71);
    CHECK(memcmp(&frame[1], interleaved, sizeof(interleaved)) == 0);

    /* The read mask must never leak into a write address. */
    len = bme68x_spi_write_frame(BME68X_REG_CTRL_MEAS | BME68X_SPI_RD_MSK, cap, 1, frame,
                                 sizeof(frame));
    CHECK(len == 2);
    CHECK(frame[0] == BME68X_REG_CTRL_MEAS);

    /* Boundaries. */
    uint8_t big[BME68X_SPI_REG_MAX_BYTES] = { 0 };
    len = bme68x_spi_write_frame(0x00, big, sizeof(big), frame, sizeof(frame));
    CHECK(len == BME68X_SPI_FRAME_MAX_BYTES);
    CHECK(bme68x_spi_write_frame(0x00, big, BME68X_SPI_FRAME_MAX_BYTES, frame, sizeof(frame)) == 0);
    CHECK(bme68x_spi_write_frame(0x00, big, 0, frame, sizeof(frame)) == 0);
    CHECK(bme68x_spi_write_frame(0x00, big, 1, frame, 1) == 0);
    CHECK(bme68x_spi_write_frame(0x00, NULL, 1, frame, sizeof(frame)) == 0);
}

/* ------------------------------------------------------------------------- */
/* Mock BME688 on the sensor side of the frame                                 */
/* ------------------------------------------------------------------------- */

struct mock_bus {
    uint8_t regs[256]; /* full 8-bit register address: 0x80..0xFF (page 0), 0x00..0x7F (page 1) */
    unsigned page;     /* status<4>: 1 = low-address registers (datasheet Table 21) */
    unsigned reads;
    unsigned writes;
    unsigned bad_frames;
    unsigned delay_calls;
    uint32_t delay_us;
};

/* status is reachable on both pages at address 0x73 and carries spi_mem_page<4>. */
static void mock_set_status(struct mock_bus *bus, uint8_t value)
{
    bus->regs[0x73] = value;
    bus->page = (value & BME68X_MEM_PAGE_MSK) ? 1U : 0U;
}

static uint16_t mock_full_addr(const struct mock_bus *bus, uint8_t addr7)
{
    if ((addr7 & 0x7FU) == 0x73) {
        return 0x73;
    }

    return (uint16_t)(bus->page ? addr7 : (uint16_t)(addr7 | 0x80U));
}

static void mock_write_reg(struct mock_bus *bus, uint8_t addr7, uint8_t value)
{
    if (addr7 == 0x73) {
        mock_set_status(bus, value);
        return;
    }

    uint16_t full = mock_full_addr(bus, addr7);
    bus->regs[full] = value;

    if ((full == BME68X_REG_SOFT_RESET) && (value == BME68X_SOFT_RESET_CMD)) {
        mock_set_status(bus, 0x00); /* reset restores spi_mem_page = 0 */
    }
}

/* Sensor-side decode of one read frame: control byte with RD set, then data. */
static int8_t mock_read(uint8_t reg_addr, uint8_t *reg_data, uint32_t len, void *intf_ptr)
{
    struct mock_bus *bus = (struct mock_bus *)intf_ptr;
    uint8_t frame[BME68X_SPI_FRAME_MAX_BYTES];
    uint32_t frame_len = bme68x_spi_read_frame(reg_addr, len, frame, sizeof(frame));

    bus->reads++;
    if ((frame_len == 0) || ((frame[0] & BME68X_SPI_RD_MSK) == 0)) {
        bus->bad_frames++;
        return -1;
    }
    for (uint32_t i = 1; i < frame_len; i++) {
        if (frame[i] != 0) {
            bus->bad_frames++; /* data must not be written while reading */
            return -1;
        }
    }

    const uint8_t addr7 = (uint8_t)(frame[0] & BME68X_SPI_WR_MSK);
    bool touches_field0 = false;

    for (uint32_t i = 0; i < len; i++) {
        uint16_t full = mock_full_addr(bus, (uint8_t)(addr7 + i));
        reg_data[i] = bus->regs[full];
        if (full == BME68X_REG_FIELD0) {
            touches_field0 = true;
        }
    }

    /* Datasheet 5.3.6.1: new_data marks data "not already read-out", so the sensor
     * clears it once the field has been read. */
    if (touches_field0) {
        bus->regs[BME68X_REG_FIELD0] &= (uint8_t)~BME68X_NEW_DATA_MSK;
    }

    return 0;
}

/* Sensor-side decode of one write frame: control byte (WR) + (addr, data) pairs. */
static int8_t mock_write(uint8_t reg_addr, const uint8_t *reg_data, uint32_t len, void *intf_ptr)
{
    struct mock_bus *bus = (struct mock_bus *)intf_ptr;
    uint8_t frame[BME68X_SPI_FRAME_MAX_BYTES];
    uint32_t frame_len = bme68x_spi_write_frame(reg_addr, reg_data, len, frame, sizeof(frame));

    bus->writes++;
    /* Control byte + 2 * len - 1 interleaved bytes = an even total length. */
    if ((frame_len == 0) || ((frame[0] & BME68X_SPI_RD_MSK) != 0) || ((frame_len % 2U) != 0)) {
        bus->bad_frames++;
        return -1;
    }

    for (uint32_t i = 0; i + 1U < frame_len; i += 2U) {
        if ((frame[i] & BME68X_SPI_RD_MSK) != 0) {
            bus->bad_frames++;
            return -1;
        }
        mock_write_reg(bus, (uint8_t)(frame[i] & BME68X_SPI_WR_MSK), frame[i + 1U]);
    }

    return 0;
}

static void mock_delay_us(uint32_t period_us, void *intf_ptr)
{
    struct mock_bus *bus = (struct mock_bus *)intf_ptr;

    bus->delay_calls++;
    bus->delay_us += period_us;
}

/*
 * Fill field 0 (forced mode uses field 0 only) with a synthetic conversion.
 * gas_flags carries gas_valid_r (0x20) and heat_stab_r (0x10).
 */
static void mock_set_field(struct mock_bus *bus,
                           uint8_t meas_index,
                           uint32_t pres_adc,
                           uint32_t temp_adc,
                           uint16_t hum_adc,
                           uint16_t gas_adc,
                           uint8_t gas_range,
                           uint8_t gas_flags)
{
    bus->regs[BME68X_REG_FIELD0 + 0] = (uint8_t)(BME68X_NEW_DATA_MSK | 0x00);
    bus->regs[BME68X_REG_FIELD0 + 1] = meas_index;
    bus->regs[BME68X_REG_FIELD0 + 2] = (uint8_t)(pres_adc >> 12);
    bus->regs[BME68X_REG_FIELD0 + 3] = (uint8_t)(pres_adc >> 4);
    bus->regs[BME68X_REG_FIELD0 + 4] = (uint8_t)((pres_adc & 0x0FU) << 4);
    bus->regs[BME68X_REG_FIELD0 + 5] = (uint8_t)(temp_adc >> 12);
    bus->regs[BME68X_REG_FIELD0 + 6] = (uint8_t)(temp_adc >> 4);
    bus->regs[BME68X_REG_FIELD0 + 7] = (uint8_t)((temp_adc & 0x0FU) << 4);
    bus->regs[BME68X_REG_FIELD0 + 8] = (uint8_t)(hum_adc >> 8);
    bus->regs[BME68X_REG_FIELD0 + 9] = (uint8_t)(hum_adc & 0xFFU);
    bus->regs[BME68X_REG_FIELD0 + 13] = (uint8_t)(gas_adc >> 2);
    bus->regs[BME68X_REG_FIELD0 + 14] =
        (uint8_t)(((gas_adc & 0x03U) << 6) | gas_flags | (gas_range & BME68X_GAS_RANGE_MSK));
}

/* ------------------------------------------------------------------------- */
/* Bosch API contract                                                        */
/* ------------------------------------------------------------------------- */

/*
 * Synthetic calibration with a closed-form expected output:
 *   temperature 31.25 degC  (temp_adc 655360, par_t1 = 0, par_t2 = 4000)
 *   pressure    101325 Pa   (pres_adc 562216, par_p1 = 30000, rest 0)
 *   humidity    50 %RH      (hum_adc 400, par_h2 = 32768, rest 0)
 *   gas         8 MOhm      (gas_adc 512, gas_range 0, range_sw_err 0)
 */
static void mock_set_calibration(struct bme68x_dev *dev)
{
    dev->calib.par_t1 = 0;
    dev->calib.par_t2 = 4000;
    dev->calib.par_t3 = 0;
    dev->calib.par_p1 = 30000;
    dev->calib.par_p2 = 0;
    dev->calib.par_p3 = 0;
    dev->calib.par_p4 = 0;
    dev->calib.par_p5 = 0;
    dev->calib.par_p6 = 0;
    dev->calib.par_p7 = 0;
    dev->calib.par_p8 = 0;
    dev->calib.par_p9 = 0;
    dev->calib.par_p10 = 0;
    dev->calib.par_h1 = 0;
    dev->calib.par_h2 = 32768;
    dev->calib.par_h3 = 0;
    dev->calib.par_h4 = 0;
    dev->calib.par_h5 = 0;
    dev->calib.par_h6 = 0;
    dev->calib.par_h7 = 0;
    dev->calib.par_gh1 = 0;
    dev->calib.par_gh2 = 0;
    dev->calib.par_gh3 = 0;
    dev->calib.res_heat_range = 0;
    dev->calib.res_heat_val = 0;
    dev->calib.range_sw_err = 0;
}

static void test_bosch_api_contract(void)
{
    struct mock_bus bus;
    struct bme68x_dev dev;

    memset(&bus, 0, sizeof(bus));
    memset(&dev, 0, sizeof(dev));

    bus.regs[BME68X_REG_CHIP_ID] = BME68X_CHIP_ID;
    bus.regs[BME68X_REG_VARIANT_ID] = BME68X_VARIANT_GAS_LOW;
    mock_set_status(&bus, 0x00); /* power-on: spi_mem_page = 0 */

    dev.intf = BME68X_SPI_INTF;
    dev.read = mock_read;
    dev.write = mock_write;
    dev.delay_us = mock_delay_us;
    dev.intf_ptr = &bus;
    dev.amb_temp = 25;

    CHECK(bme68x_init(&dev) == BME68X_OK);
    CHECK(dev.chip_id == BME68X_CHIP_ID);
    CHECK(dev.variant_id == BME68X_VARIANT_GAS_LOW);
    CHECK(bus.bad_frames == 0);
    CHECK(bus.reads > 0 && bus.writes > 0);

    mock_set_calibration(&dev);

    struct bme68x_conf conf = {
        .os_temp = BME68X_OS_2X,
        .os_pres = BME68X_OS_4X,
        .os_hum = BME68X_OS_2X,
        .filter = BME68X_FILTER_SIZE_3,
        .odr = BME68X_ODR_NONE,
    };
    CHECK(bme68x_set_conf(&conf, &dev) == BME68X_OK);
    CHECK(bus.bad_frames == 0);

    /* Read-back only matches if the five-register interleaved write frame really
     * carried every (address, data) pair in one transfer. */
    struct bme68x_conf applied;
    memset(&applied, 0, sizeof(applied));
    CHECK(bme68x_get_conf(&applied, &dev) == BME68X_OK);
    CHECK(applied.os_temp == conf.os_temp);
    CHECK(applied.os_pres == conf.os_pres);
    CHECK(applied.os_hum == conf.os_hum);
    CHECK(applied.filter == conf.filter);
    CHECK(applied.odr == conf.odr);

    const struct bme68x_heatr_conf heatr = {
        .enable = BME68X_ENABLE,
        .heatr_temp = 320,
        .heatr_dur = 150,
    };
    CHECK(bme68x_set_heatr_conf(BME68X_FORCED_MODE, &heatr, &dev) == BME68X_OK);
    CHECK(bus.bad_frames == 0);
    /* gas_wait_0 (page 1) was written by set_heatr_conf. bme68x_esp_measure() uses
     * the requested heatr_dur as the worst case, which is only valid if the encoded
     * on-time (value * 4^factor * 0.477 ms) never exceeds it. */
    const uint8_t gas_wait = bus.regs[BME68X_REG_GAS_WAIT0];
    const uint32_t encoded_dur_us = ((uint32_t)(gas_wait & 0x3FU) << (2U * (gas_wait >> 6))) * 477U;
    CHECK(gas_wait != 0);
    CHECK(encoded_dur_us <= 150U * 1000U);

    CHECK(bme68x_set_op_mode(BME68X_FORCED_MODE, &dev) == BME68X_OK);
    CHECK(bus.bad_frames == 0);

    mock_set_field(&bus, 42, 562216, 655360, 400, 512, 0,
                   BME68X_GASM_VALID_MSK | BME68X_HEAT_STAB_MSK);

    struct bme68x_data data;
    uint8_t n_data = 0;
    memset(&data, 0, sizeof(data));
    CHECK(bme68x_get_data(BME68X_FORCED_MODE, &data, &n_data, &dev) == BME68X_OK);
    CHECK(n_data == 1);
    CHECK(data.gas_index == 0);
    CHECK(data.meas_index == 42);
    CHECK((data.status & BME68X_NEW_DATA_MSK) != 0);
    CHECK((data.status & BME68X_GASM_VALID_MSK) != 0);
    CHECK((data.status & BME68X_HEAT_STAB_MSK) != 0);

#ifdef BME68X_USE_FPU
    /* bme68x_esp.c forwards these values unchanged (float build). */
    CHECK(fabsf(data.temperature - 31.25f) < 0.01f);
    CHECK(fabsf(data.pressure - 101325.0f) < 2.0f);
    CHECK(fabsf(data.humidity - 50.0f) < 0.01f);
    CHECK(fabsf(data.gas_resistance - 8000000.0f) < 2.0f);
#else
    /* bme68x_esp.c divides temperature by 100 and humidity by 1000. */
    CHECK(data.temperature == 3125);
    CHECK(data.pressure >= 101323 && data.pressure <= 101327);
    CHECK(data.humidity == 50000);
    CHECK(data.gas_resistance == 8000000);
#endif

    /* The field read above cleared new_data: a further read must report "no new
     * data" and leave the output untouched. This is the condition
     * bme68x_esp_measure()'s poll loop treats as "conversion still running". */
    struct bme68x_data stale;
    unsigned delay_before = bus.delay_calls;
    memset(&stale, 0, sizeof(stale));
    n_data = 99;
    CHECK(bme68x_get_data(BME68X_FORCED_MODE, &stale, &n_data, &dev) == BME68X_W_NO_NEW_DATA);
    CHECK(n_data == 0);
    CHECK((stale.status & BME68X_NEW_DATA_MSK) == 0);
    CHECK(stale.temperature == 0); /* no compensation without new_data */
    CHECK(stale.humidity == 0);
    CHECK(bus.delay_calls > delay_before); /* vendor retried instead of returning data */

    /* A conversion that completed after the previous call returned (leftover
     * new_data) is handed back immediately by get_data(). bme68x_esp_measure()
     * therefore waits out conversion + heater time before its first read, so field
     * 0 always holds the measurement that this call triggered. */
    mock_set_field(&bus, 43, 562216, 655360, 400, 512, 0,
                   BME68X_GASM_VALID_MSK | BME68X_HEAT_STAB_MSK);
    memset(&data, 0, sizeof(data));
    n_data = 0;
    CHECK(bme68x_get_data(BME68X_FORCED_MODE, &data, &n_data, &dev) == BME68X_OK);
    CHECK(n_data == 1);
    CHECK(data.meas_index == 43); /* the earlier, not this call's conversion */

    /* Gas flags are decoded from the field status byte, not assumed. */
    mock_set_field(&bus, 44, 562216, 655360, 400, 512, 0, 0x00);
    memset(&data, 0, sizeof(data));
    n_data = 0;
    CHECK(bme68x_get_data(BME68X_FORCED_MODE, &data, &n_data, &dev) == BME68X_OK);
    CHECK(n_data == 1);
    CHECK((data.status & BME68X_GASM_VALID_MSK) == 0);
    CHECK((data.status & BME68X_HEAT_STAB_MSK) == 0);

    CHECK(bus.bad_frames == 0);
}

int main(void)
{
    test_read_frame();
    test_write_frame();
    test_bosch_api_contract();

#ifdef BME68X_USE_FPU
    const char *build = "float (BME68X_USE_FPU)";
#else
    const char *build = "integer (BME68X_DO_NOT_USE_FPU)";
#endif

    if (failures != 0) {
        printf("%d check(s) failed in the %s build\n", failures, build);
        return 1;
    }

    printf("all BME68x host checks passed in the %s build\n", build);
    return 0;
}
