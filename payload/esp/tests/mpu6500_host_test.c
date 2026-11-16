/*
 * SPDX-FileCopyrightText: 2024 ECE Balloon Bot Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Host (no ESP-IDF, no hardware) regression test for the vendored MPU-6500
 * driver (drivers/mpu6500/src/driver_mpu6500.c) as the active integration uses
 * it. It replaces the MPU-9250 driver test as the test of the running sensor and
 * drives the unmodified upstream driver through the same LibDriver callback
 * contract drivers/mpu6500/mpu6500_esp.c implements:
 *
 *  - iic_read/iic_write receive the 8-bit write address (0xD0 = AD0 low, 0xD2 =
 *    AD0 high), a register address and a length, and report a failed transfer by
 *    returning 1,
 *  - the SPI hooks are linked but never reached (this integration is I2C only),
 *  - delay_ms is counted instead of slept.
 *
 * The register model in tests/mpu6500_mock_device.h answers those transfers the
 * way the chip does (auto-incremented burst reads, read-only WHO_AM_I,
 * self-clearing DEVICE_RESET), so the checks below pin down the behavior the
 * adapter and the application depend on:
 *
 *  1. I2C address selection: AD0 low/high must reach the sensor at 0xD0/0xD2; an
 *     address the mock does not decode must fail.
 *  2. Identity: WHO_AM_I 0x70 accepted, 0x71 (the MPU-9250 ID) and other values
 *     rejected with status 5, the bus released and nothing configured.
 *  3. Transport and reset failures: a failed WHO_AM_I read, an I2C init that
 *     cannot create the bus, a failed reset write and a reset that never clears,
 *     each with the status mpu6500_esp_init() maps to an ESP error.
 *  4. The settings the adapter applies (awake, PLL clock, 50 Hz divider, DLPF 3,
 *     both ranges) are written to the registers the getters read them back from.
 *  5. Six-axis raw-to-physical scaling for the configured ranges (16384/8192/
 *     4096/2048 LSB per g, 131/65.5/32.8/16.4 LSB per dps) - the numbers
 *     mpu6500_esp_read() copies into mpu6500_esp_reading_t. There is no
 *     magnetometer: the MPU-6500 is a six-axis part.
 *  6. A failed sample read is reported as status 1, which the adapter maps to
 *     ESP_ERR_INVALID_RESPONSE, plus the argument guards and power-down.
 *
 * The ESP-facing side of the same integration (the real adapter under a mock
 * ESP-IDF) is covered by tests/mpu6500_esp_adapter_host_test.c.
 *
 * Build and run with tests/run_mpu6500_host_test.sh.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "driver_mpu6500.h"
#include "mpu6500_mock_device.h"

static int failures;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);         \
            failures++;                                                    \
        }                                                                  \
    } while (0)

static void check_near(const char *file, int line, const char *expr, float actual, float expected,
                       float tolerance)
{
    if (!((actual - expected) <= tolerance) || !((expected - actual) <= tolerance)) {
        printf("FAIL %s:%d: %s = %.6f, expected %.6f +- %.6f\n", file, line, expr, (double)actual,
               (double)expected, (double)tolerance);
        failures++;
    }
}

#define CHECK_NEAR(actual, expected, tolerance) \
    check_near(__FILE__, __LINE__, #actual, (actual), (expected), (tolerance))

/* 8-bit write addresses the upstream callbacks pass (see a_mpu6500_read() and
 * a_mpu6500_write() in driver_mpu6500.c). */
#define MOCK_ADDR_AD0_LOW  0xD0U
#define MOCK_ADDR_AD0_HIGH 0xD2U

/* ------------------------------------------------------------------------- */
/* Mock MPU-6500 behind the LibDriver callbacks                              */
/* ------------------------------------------------------------------------- */

struct mock_bus {
    mpu6500_mock_device_t chip;
    uint8_t addr; /* 8-bit write address the mock sensor answers to */

    unsigned iic_init_calls;
    unsigned iic_deinit_calls;
    unsigned reads;
    unsigned writes;
    unsigned wrong_addr_calls;
    unsigned last_wrong_addr;
    unsigned delay_calls;

    bool fail_iic_init; /* iic_init() reports a bus that cannot be created */
    bool fail_reads;    /* every read transfer fails (device absent) */
    bool fail_writes;   /* every write fails (read-only device path) */
    int fail_read_reg;  /* any read transfer covering this register fails, -1 = none */
};

/* The LibDriver callbacks carry no context argument, so - like mpu6500_esp.c
 * with its s_ctx - the mock keeps the single active device in a global. */
static struct mock_bus g_bus;

static uint8_t mock_iic_init(void)
{
    g_bus.iic_init_calls++;
    return g_bus.fail_iic_init ? 1 : 0;
}

static uint8_t mock_iic_deinit(void)
{
    g_bus.iic_deinit_calls++;
    return 0;
}

/* A burst read fails as a whole when the injected register falls inside the
 * range it covers. */
static bool read_fails(uint8_t reg, uint16_t len)
{
    if (g_bus.fail_reads) {
        return true;
    }

    return (g_bus.fail_read_reg >= (int)reg) && (g_bus.fail_read_reg < ((int)reg + (int)len));
}

static uint8_t mock_iic_read(uint8_t addr, uint8_t reg, uint8_t *buf, uint16_t len)
{
    if (addr != g_bus.addr) {
        g_bus.wrong_addr_calls++;
        g_bus.last_wrong_addr = addr;
        return 1; /* nobody on the bus at this address */
    }
    if (read_fails(reg, len)) {
        return 1;
    }

    g_bus.reads++;
    mpu6500_mock_device_burst_read(&g_bus.chip, reg, buf, len);

    return 0;
}

static uint8_t mock_iic_write(uint8_t addr, uint8_t reg, uint8_t *buf, uint16_t len)
{
    if (addr != g_bus.addr) {
        g_bus.wrong_addr_calls++;
        g_bus.last_wrong_addr = addr;
        return 1;
    }
    if (g_bus.fail_writes) {
        return 1;
    }

    g_bus.writes++;
    mpu6500_mock_device_burst_write(&g_bus.chip, reg, buf, len);

    return 0;
}

static void mock_delay_ms(uint32_t ms)
{
    g_bus.delay_calls++;
    (void)ms; /* counted, never slept: the init polling loops run instantly */
}

static void mock_debug_print(const char *const fmt, ...)
{
    (void)fmt; /* the upstream driver's failure descriptions are not assertions */
}

static void mock_receive_callback(uint8_t type)
{
    (void)type; /* polling only: no interrupt pin is wired */
}

/* SPI is never reached in this I2C-only integration; the hooks exist because
 * mpu6500_init() validates every linked pointer before it looks at iic_spi. */
static uint8_t mock_spi_init(void)
{
    return 1;
}

static uint8_t mock_spi_deinit(void)
{
    return 1;
}

static uint8_t mock_spi_read(uint8_t reg, uint8_t *buf, uint16_t len)
{
    (void)reg;
    (void)buf;
    (void)len;

    return 1;
}

static uint8_t mock_spi_write(uint8_t reg, uint8_t *buf, uint16_t len)
{
    (void)reg;
    (void)buf;
    (void)len;

    return 1;
}

/* Power-on state: an MPU-6500 at 0xD0 with a valid ID and no faults. */
static void mock_reset(void)
{
    memset(&g_bus, 0, sizeof(g_bus));

    mpu6500_mock_device_power_on(&g_bus.chip);
    g_bus.addr = MOCK_ADDR_AD0_LOW;
    g_bus.fail_read_reg = -1;
}

/* Link the callbacks the way mpu6500_esp_init() does. */
static void link_handle(mpu6500_handle_t *handle, mpu6500_address_t addr_pin)
{
    DRIVER_MPU6500_LINK_INIT(handle, mpu6500_handle_t);
    DRIVER_MPU6500_LINK_IIC_INIT(handle, mock_iic_init);
    DRIVER_MPU6500_LINK_IIC_DEINIT(handle, mock_iic_deinit);
    DRIVER_MPU6500_LINK_IIC_READ(handle, mock_iic_read);
    DRIVER_MPU6500_LINK_IIC_WRITE(handle, mock_iic_write);
    DRIVER_MPU6500_LINK_SPI_INIT(handle, mock_spi_init);
    DRIVER_MPU6500_LINK_SPI_DEINIT(handle, mock_spi_deinit);
    DRIVER_MPU6500_LINK_SPI_READ(handle, mock_spi_read);
    DRIVER_MPU6500_LINK_SPI_WRITE(handle, mock_spi_write);
    DRIVER_MPU6500_LINK_DELAY_MS(handle, mock_delay_ms);
    DRIVER_MPU6500_LINK_DEBUG_PRINT(handle, mock_debug_print);
    DRIVER_MPU6500_LINK_RECEIVE_CALLBACK(handle, mock_receive_callback);

    CHECK(mpu6500_set_interface(handle, MPU6500_INTERFACE_IIC) == 0);
    CHECK(mpu6500_set_addr_pin(handle, addr_pin) == 0);
}

/* ------------------------------------------------------------------------- */
/* I2C address selection                                                      */
/* ------------------------------------------------------------------------- */

static void test_i2c_address_selection(void)
{
    mpu6500_handle_t handle;

    /* AD0 low: every transfer goes to 0xD0. */
    mock_reset();
    link_handle(&handle, MPU6500_ADDRESS_AD0_LOW);
    CHECK(mpu6500_init(&handle) == 0);
    CHECK(g_bus.reads > 0);
    CHECK(g_bus.wrong_addr_calls == 0);

    /* AD0 high against a device wired for AD0 low: the 8-bit address really is
     * applied to the transfer, so nobody answers and WHO_AM_I cannot be read. */
    mock_reset();
    link_handle(&handle, MPU6500_ADDRESS_AD0_HIGH);
    CHECK(mpu6500_init(&handle) == 5);
    CHECK(g_bus.wrong_addr_calls > 0);
    CHECK(g_bus.last_wrong_addr == MOCK_ADDR_AD0_HIGH);
    CHECK(handle.inited == 0);

    /* AD0 high against a device wired for AD0 high: 0xD2 is decoded. */
    mock_reset();
    g_bus.addr = MOCK_ADDR_AD0_HIGH;
    link_handle(&handle, MPU6500_ADDRESS_AD0_HIGH);
    CHECK(mpu6500_init(&handle) == 0);
    CHECK(g_bus.reads > 0);
    CHECK(g_bus.wrong_addr_calls == 0);
}

/* ------------------------------------------------------------------------- */
/* Identity                                                                  */
/* ------------------------------------------------------------------------- */

static void test_init_accepts_mpu6500(void)
{
    mpu6500_handle_t handle;

    mock_reset();
    link_handle(&handle, MPU6500_ADDRESS_AD0_LOW);
    CHECK(mpu6500_init(&handle) == 0);
    CHECK(handle.inited == 1);
    CHECK(handle.dmp_inited == 0);
    CHECK(g_bus.iic_init_calls == 1);
    CHECK(g_bus.chip.reset_writes == 1); /* DEVICE_RESET written once ... */
    CHECK((g_bus.chip.reg[MPU6500_MOCK_REG_PWR_MGMT_1] & 0x80U) == 0); /* ... and polled clear */
    CHECK(g_bus.delay_calls == 1); /* the reset settling delay was requested */
}

static void test_init_rejects_other_who_am_i(void)
{
    /* 0x71 is the MPU-9250 ID the previous integration accepted; 0x70 is the only
     * ID an MPU-6500 reports. */
    static const uint8_t invalid_ids[] = {
        MPU6500_MOCK_WHO_AM_I_MPU9250, 0x00, 0x68, 0xFF
    };

    for (unsigned i = 0; i < sizeof(invalid_ids); i++) {
        mpu6500_handle_t handle;

        mock_reset();
        g_bus.chip.who_am_i = invalid_ids[i];
        link_handle(&handle, MPU6500_ADDRESS_AD0_LOW);
        CHECK(mpu6500_init(&handle) == 5);
        CHECK(handle.inited == 0);
        CHECK(g_bus.iic_deinit_calls == 1); /* bus released on the failure path */
        CHECK(g_bus.writes == 0);           /* nothing configured on a wrong ID */
        CHECK(g_bus.chip.reset_writes == 0);
    }
}

/* ------------------------------------------------------------------------- */
/* Failure paths                                                             */
/* ------------------------------------------------------------------------- */

static void test_init_reports_transport_failure(void)
{
    mpu6500_handle_t handle;

    /* The WHO_AM_I read itself fails: status 5 ("read who am i failed"), the same
     * status a wrong ID produces. mpu6500_esp_init() maps it to ESP_ERR_NOT_FOUND,
     * but in the adapter the diagnostic preflight read runs first and reports the
     * failed transfer as ESP_ERR_INVALID_RESPONSE - which is why the adapter test
     * asserts that code instead (tests/mpu6500_esp_adapter_host_test.c). */
    mock_reset();
    g_bus.fail_reads = true;
    link_handle(&handle, MPU6500_ADDRESS_AD0_LOW);
    CHECK(mpu6500_init(&handle) == 5);
    CHECK(handle.inited == 0);
    CHECK(g_bus.iic_deinit_calls == 1);

    /* The bus cannot be created at all: status 1 (ESP_FAIL). */
    mock_reset();
    g_bus.fail_iic_init = true;
    link_handle(&handle, MPU6500_ADDRESS_AD0_LOW);
    CHECK(mpu6500_init(&handle) == 1);
    CHECK(handle.inited == 0);
    CHECK(g_bus.iic_deinit_calls == 0); /* nothing was initialized to tear down */
    CHECK(g_bus.reads == 0);

    /* The reset write fails: status 4 ("write pwr mgmt 1 failed"). */
    mock_reset();
    g_bus.fail_writes = true;
    link_handle(&handle, MPU6500_ADDRESS_AD0_LOW);
    CHECK(mpu6500_init(&handle) == 4);
    CHECK(handle.inited == 0);
    CHECK(g_bus.iic_deinit_calls == 1);
}

static void test_init_reports_stuck_reset(void)
{
    mpu6500_handle_t handle;

    /* A device that never clears DEVICE_RESET: the driver polls for 1000 ms and
     * gives up with status 4 without calling the deinit hook, so the adapter's
     * own release is what frees the bus. */
    mock_reset();
    g_bus.chip.stuck_reset_register = MPU6500_MOCK_REG_PWR_MGMT_1;
    link_handle(&handle, MPU6500_ADDRESS_AD0_LOW);
    CHECK(mpu6500_init(&handle) == 4);
    CHECK(handle.inited == 0);
    CHECK(g_bus.chip.reset_writes == 1);
    CHECK(g_bus.delay_calls == 101); /* the 10 ms settling delay plus 100 polls */
    CHECK(g_bus.iic_deinit_calls == 0);
}

/*
 * The settings mpu6500_esp_init() applies after mpu6500_init(), read back through
 * the getters. mpu6500_init() itself configures nothing, so a dropped write (or a
 * setter that writes the wrong bits) fails these checks.
 */
static void test_applies_adapter_configuration(void)
{
    mpu6500_handle_t handle;

    mock_reset();
    link_handle(&handle, MPU6500_ADDRESS_AD0_LOW);
    CHECK(mpu6500_init(&handle) == 0);

    /* Wake up, PLL clock, 1 kHz / 20 = 50 Hz, DLPF 3, the adapter's ranges. */
    CHECK(mpu6500_set_sleep(&handle, MPU6500_BOOL_FALSE) == 0);
    CHECK(mpu6500_set_clock_source(&handle, MPU6500_CLOCK_SOURCE_PLL) == 0);
    CHECK(mpu6500_set_sample_rate_divider(&handle, 19) == 0);
    CHECK(mpu6500_set_low_pass_filter(&handle, MPU6500_LOW_PASS_FILTER_3) == 0);
    CHECK(mpu6500_set_accelerometer_range(&handle, MPU6500_ACCELEROMETER_RANGE_2G) == 0);
    CHECK(mpu6500_set_gyroscope_range(&handle, MPU6500_GYROSCOPE_RANGE_2000DPS) == 0);

    mpu6500_bool_t sleep = MPU6500_BOOL_TRUE;
    mpu6500_clock_source_t clock = MPU6500_CLOCK_SOURCE_STOP_CLOCK;
    uint8_t divider = 0;
    mpu6500_low_pass_filter_t filter = MPU6500_LOW_PASS_FILTER_0;
    mpu6500_accelerometer_range_t accel_range = MPU6500_ACCELEROMETER_RANGE_16G;
    mpu6500_gyroscope_range_t gyro_range = MPU6500_GYROSCOPE_RANGE_250DPS;

    CHECK(mpu6500_get_sleep(&handle, &sleep) == 0);
    CHECK(sleep == MPU6500_BOOL_FALSE);
    CHECK(mpu6500_get_clock_source(&handle, &clock) == 0);
    CHECK(clock == MPU6500_CLOCK_SOURCE_PLL);
    CHECK(mpu6500_get_sample_rate_divider(&handle, &divider) == 0);
    CHECK(divider == 19);
    CHECK(mpu6500_get_low_pass_filter(&handle, &filter) == 0);
    CHECK(filter == MPU6500_LOW_PASS_FILTER_3);
    CHECK(mpu6500_get_accelerometer_range(&handle, &accel_range) == 0);
    CHECK(accel_range == MPU6500_ACCELEROMETER_RANGE_2G);
    CHECK(mpu6500_get_gyroscope_range(&handle, &gyro_range) == 0);
    CHECK(gyro_range == MPU6500_GYROSCOPE_RANGE_2000DPS);

    /* The range setters own bits 4:3 of the two config registers. */
    CHECK((g_bus.chip.reg[MPU6500_MOCK_REG_ACCEL_CONFIG] >> 3) == 0x00U);
    CHECK((g_bus.chip.reg[MPU6500_MOCK_REG_GYRO_CONFIG] >> 3) == 0x03U);
}

/* ------------------------------------------------------------------------- */
/* Six-axis read and scaling                                                 */
/* ------------------------------------------------------------------------- */

struct axis_case {
    const char *name;
    mpu6500_accelerometer_range_t accel_range;
    mpu6500_gyroscope_range_t gyro_range;
    int16_t accel_raw[3];
    float accel_g[3];
    int16_t gyro_raw[3];
    float gyro_dps[3];
};

/*
 * Raw counts and the physical values they must produce. The first case is the
 * adapter's default configuration; the others change both ranges, which only
 * works if the driver takes the scale factors from the ACCEL_CONFIG/GYRO_CONFIG
 * registers it reads on every call.
 */
static const struct axis_case axis_cases[] = {
    /* +-2 g (16384 LSB/g), +-2000 dps (16.4 LSB/dps). */
    { "2 g / 2000 dps",
      MPU6500_ACCELEROMETER_RANGE_2G,
      MPU6500_GYROSCOPE_RANGE_2000DPS,
      { 16384, -8192, 4096 },
      { 1.0f, -0.5f, 0.25f },
      { 164, -328, 41 },
      { 10.0f, -20.0f, 2.5f } },
    /* +-8 g (4096 LSB/g), +-500 dps (65.5 LSB/dps). */
    { "8 g / 500 dps",
      MPU6500_ACCELEROMETER_RANGE_8G,
      MPU6500_GYROSCOPE_RANGE_500DPS,
      { 8192, -4096, 1024 },
      { 2.0f, -1.0f, 0.25f },
      { 655, -131, 131 },
      { 10.0f, -2.0f, 2.0f } },
    /* +-16 g (2048 LSB/g), the power-on gyro range +-250 dps (131 LSB/dps). */
    { "16 g / 250 dps",
      MPU6500_ACCELEROMETER_RANGE_16G,
      MPU6500_GYROSCOPE_RANGE_250DPS,
      { -2048, 4096, 0 },
      { -1.0f, 2.0f, 0.0f },
      { 131, -262, 1310 },
      { 1.0f, -2.0f, 10.0f } },
};

static void test_read_six_axis_scaling(void)
{
    mpu6500_handle_t handle;

    mock_reset();
    link_handle(&handle, MPU6500_ADDRESS_AD0_LOW);
    CHECK(mpu6500_init(&handle) == 0);

    for (unsigned c = 0; c < sizeof(axis_cases) / sizeof(axis_cases[0]); c++) {
        const struct axis_case *tc = &axis_cases[c];

        CHECK(mpu6500_set_accelerometer_range(&handle, tc->accel_range) == 0);
        CHECK(mpu6500_set_gyroscope_range(&handle, tc->gyro_range) == 0);
        mpu6500_mock_device_set_motion(&g_bus.chip, tc->accel_raw, tc->gyro_raw);

        int16_t accel_raw[1][3] = { { 0 } };
        int16_t gyro_raw[1][3] = { { 0 } };
        float accel_g[1][3] = { { 0.0f } };
        float gyro_dps[1][3] = { { 0.0f } };
        uint16_t len = 1;

        CHECK(mpu6500_read(&handle, accel_raw, accel_g, gyro_raw, gyro_dps, &len) == 0);
        CHECK(len == 1);

        if (len != 1) {
            continue; /* the remaining checks would read unset values */
        }

        for (unsigned axis = 0; axis < 3; axis++) {
            if ((accel_raw[0][axis] != tc->accel_raw[axis]) ||
                (gyro_raw[0][axis] != tc->gyro_raw[axis])) {
                printf("FAIL %s: axis %u raw counts a=%d w=%d\n", tc->name, axis,
                       accel_raw[0][axis], gyro_raw[0][axis]);
                failures++;
            }
            /* The expected values are exact binary quotients, so the tolerance only
             * absorbs float rounding - not a wrong scale factor. */
            CHECK_NEAR(accel_g[0][axis], tc->accel_g[axis], 0.00001f);
            CHECK_NEAR(gyro_dps[0][axis], tc->gyro_dps[axis], 0.00001f);
        }
    }
}

static void test_read_reports_transport_failure(void)
{
    mpu6500_handle_t handle;

    mock_reset();
    link_handle(&handle, MPU6500_ADDRESS_AD0_LOW);
    CHECK(mpu6500_init(&handle) == 0);

    /* The 14-byte accel/gyro/temp block read fails: status 1, which
     * mpu6500_esp_read() maps to ESP_ERR_INVALID_RESPONSE. */
    g_bus.fail_read_reg = MPU6500_MOCK_REG_ACCEL_XOUT_H;

    int16_t accel_raw[1][3] = { { 0 } };
    int16_t gyro_raw[1][3] = { { 0 } };
    float accel_g[1][3] = { { 0.0f } };
    float gyro_dps[1][3] = { { 0.0f } };
    uint16_t len = 1;

    CHECK(mpu6500_read(&handle, accel_raw, accel_g, gyro_raw, gyro_dps, &len) == 1);
}

static void test_read_argument_guards(void)
{
    mpu6500_handle_t handle;

    int16_t accel_raw[1][3] = { { 0 } };
    int16_t gyro_raw[1][3] = { { 0 } };
    float accel_g[1][3] = { { 0.0f } };
    float gyro_dps[1][3] = { { 0.0f } };
    uint16_t len = 1;

    /* Handle NULL: status 2 (ESP_ERR_INVALID_ARG for the adapter). */
    CHECK(mpu6500_read(NULL, accel_raw, accel_g, gyro_raw, gyro_dps, &len) == 2);

    /* Not initialized: status 3 (ESP_ERR_INVALID_STATE). */
    mock_reset();
    link_handle(&handle, MPU6500_ADDRESS_AD0_LOW);
    CHECK(mpu6500_read(&handle, accel_raw, accel_g, gyro_raw, gyro_dps, &len) == 3);

    /* Zero length: status 4 (ESP_ERR_INVALID_SIZE). */
    CHECK(mpu6500_init(&handle) == 0);
    len = 0;
    CHECK(mpu6500_read(&handle, accel_raw, accel_g, gyro_raw, gyro_dps, &len) == 4);
}

static void test_deinit_powers_down(void)
{
    mpu6500_handle_t handle;

    mock_reset();
    link_handle(&handle, MPU6500_ADDRESS_AD0_LOW);
    CHECK(mpu6500_init(&handle) == 0);

    CHECK(mpu6500_deinit(&handle) == 0);
    CHECK(handle.inited == 0);
    /* Sleep mode with the clock stopped, then the bus released. */
    CHECK((g_bus.chip.reg[MPU6500_MOCK_REG_PWR_MGMT_1] & (1U << 6)) != 0);
    CHECK((g_bus.chip.reg[MPU6500_MOCK_REG_PWR_MGMT_1] & 0x07U) == 0x07U);
    CHECK(g_bus.iic_deinit_calls == 1);
}

int main(void)
{
    test_i2c_address_selection();
    test_init_accepts_mpu6500();
    test_init_rejects_other_who_am_i();
    test_init_reports_transport_failure();
    test_init_reports_stuck_reset();
    test_applies_adapter_configuration();
    test_read_six_axis_scaling();
    test_read_reports_transport_failure();
    test_read_argument_guards();
    test_deinit_powers_down();

    if (failures != 0) {
        printf("%d check(s) failed in the MPU-6500 driver host test\n", failures);
        return 1;
    }

    printf("all MPU-6500 driver host checks passed\n");
    return 0;
}
