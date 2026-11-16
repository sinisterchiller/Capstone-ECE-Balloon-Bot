/*
 * SPDX-FileCopyrightText: 2024 ECE Balloon Bot Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Host (no ESP-IDF, no hardware) regression test for the MPU-9250 integration.
 *
 * mpu9250_esp.c cannot run on the host (I2C master, esp_timer, logging), so this
 * test drives the unmodified upstream driver (drivers/mpu9250/src/driver_mpu9250.c)
 * through the same LibDriver callback contract the adapter implements:
 *
 *  - iic_read/iic_write receive the 8-bit write address (0xD0 = MPU-9250 with AD0
 *    low, 0x18 = AK8963 behind the MPU bypass), a register address and a length,
 *    and report a failed transfer by returning 1,
 *  - the SPI hooks are linked but never reached (this integration is I2C only),
 *  - delay_ms is counted instead of slept.
 *
 * A mock MPU-9250 + AK8963 register file answers those transfers the way the two
 * chips do (auto-incremented burst reads, self-clearing DEVICE_RESET and SRST
 * bits, read-only identification registers), so the checks below pin down the
 * behavior mpu9250_esp.c and the application depend on:
 *
 *  1. I2C address selection: AD0 low/high must reach the sensor at 0xD0/0xD2 and
 *     the AK8963 at 0x18 only; an address the mock does not decode must fail.
 *  2. MPU-9250 init: WHO_AM_I 0x71 accepted, the device reset really polled, a
 *     wrong WHO_AM_I (status 5) and a failed transfer (status 5, status 1 from
 *     iic_init) rejected with the bus released.
 *  3. The settings mpu9250_esp_init() applies (sleep, clock source, sample rate
 *     divider, DLPF, both ranges) are written to the registers the getters read
 *     them back from.
 *  4. AK8963 init: WIA 0x48 accepted, the fuse-ROM sensitivity adjustment (ASA)
 *     read back into handle.mag_asa, the bypass enabled, a wrong WIA (status 5)
 *     and a failed ASA read (status 1) rejected.
 *  5. Raw-to-physical scaling of all nine axes for the configured ranges
 *     (16384/8192 LSB per g, 131/65.5/16.4 LSB per dps, 0.15 uT per 16-bit LSB
 *     times the per-axis ASA factor, 0.6 uT per 14-bit LSB) - the numbers
 *     mpu9250_esp_read() copies into mpu9250_esp_reading_t.
 *  6. A failed data read is reported as status 1, which the adapter maps to
 *     ESP_ERR_INVALID_RESPONSE.
 *  7. Argument guards and power-down of both chips.
 *
 * Build and run with tests/run_mpu9250_host_test.sh.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "driver_mpu9250.h"

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

/* ------------------------------------------------------------------------- */
/* Registers the tests touch                                                   */
/* ------------------------------------------------------------------------- */

/* The vendor driver keeps its register map private to driver_mpu9250.c, so the
 * mock names the registers it has to expose by address. */
#define MPU_REG_GYRO_CONFIG   0x1B
#define MPU_REG_ACCEL_CONFIG  0x1C
#define MPU_REG_INT_PIN_CFG   0x37
#define MPU_REG_ACCEL_XOUT_H  0x3B
#define MPU_REG_GYRO_XOUT_H   0x43
#define MPU_REG_USER_CTRL     0x6A
#define MPU_REG_PWR_MGMT_1    0x6B
#define MPU_REG_WHO_AM_I      0x75

#define AK_REG_WIA            0x00
#define AK_REG_ST1            0x02
#define AK_REG_HXL            0x03
#define AK_REG_ST2            0x09
#define AK_REG_CNTL1          0x0A
#define AK_REG_CNTL2          0x0B
#define AK_REG_ASAX           0x10

/* 8-bit write addresses the upstream callbacks pass (see a_mpu9250_read() and
 * a_mpu9250_mag_read() in driver_mpu9250.c). */
#define MOCK_MPU_ADDR_AD0_LOW  0xD0U
#define MOCK_MPU_ADDR_AD0_HIGH 0xD2U
#define MOCK_AK_ADDR           0x18U /* AK8963_IIC_ADDRESS << 1 */

#define MOCK_MPU_WHO_AM_I      0x71U
#define MOCK_AK_WIA            0x48U

/* Sensitivity adjustment values the mock keeps in its fuse ROM: 176/128/64 give
 * the per-axis factors 1.1875 / 1.0 / 0.75 in the driver's
 * ((asa - 128) * 0.5 / 128 + 1) formula. */
#define MOCK_ASA_X 176U
#define MOCK_ASA_Y 128U
#define MOCK_ASA_Z 64U

/* ------------------------------------------------------------------------- */
/* Mock MPU-9250 + AK8963 behind the LibDriver callbacks                       */
/* ------------------------------------------------------------------------- */

struct mock_bus {
    uint8_t mpu[128]; /* MPU-9250 register file, addresses 0x00..0x7F */
    uint8_t ak[32];   /* AK8963 register file, addresses 0x00..0x12 used */
    uint8_t who_am_i; /* read-only identification register of the MPU */
    uint8_t mpu_addr; /* 8-bit write address the mock MPU answers to */
    uint8_t ak_addr;  /* 8-bit write address the mock AK8963 answers to */

    unsigned iic_init_calls;
    unsigned iic_deinit_calls;
    unsigned mpu_reads;
    unsigned mpu_writes;
    unsigned ak_reads;
    unsigned ak_writes;
    unsigned reset_writes;
    unsigned wrong_addr_calls;
    unsigned last_wrong_addr;
    unsigned delay_calls;

    bool fail_iic_init;    /* iic_init() reports a bus that cannot be created */
    bool fail_reads;       /* every transfer fails (device absent) */
    bool fail_writes;      /* every write fails (read-only device path) */
    int fail_read_reg;     /* any transfer that covers this register fails, -1 = none */
};

/* The LibDriver callbacks carry no context argument, so - like mpu9250_esp.c
 * with its s_ctx - the mock keeps the single active device in a global. */
static struct mock_bus g_bus;

static uint8_t mock_mpu_reg(uint8_t reg)
{
    reg &= 0x7FU;
    if (reg == MPU_REG_WHO_AM_I) {
        return g_bus.who_am_i; /* read-only identification register */
    }

    return g_bus.mpu[reg];
}

/* Writing DEVICE_RESET restores the power-on register state (all zero here) and
 * the reset bit itself reads back cleared, so mpu9250_init()'s poll terminates. */
static void mock_mpu_write_reg(uint8_t reg, uint8_t value)
{
    reg &= 0x7FU;
    if (reg == MPU_REG_WHO_AM_I) {
        return; /* read-only */
    }
    if ((reg == MPU_REG_PWR_MGMT_1) && ((value & 0x80U) != 0)) {
        g_bus.reset_writes++;
        memset(g_bus.mpu, 0, sizeof(g_bus.mpu));
        return;
    }

    g_bus.mpu[reg] = value;
}

static void mock_ak_write_reg(uint8_t reg, uint8_t value)
{
    if (reg == AK_REG_CNTL2) {
        /* SRST (bit 0) is self-clearing; the driver polls it until it reads 0. */
        g_bus.ak[reg] = (uint8_t)(value & ~0x01U);
        return;
    }

    g_bus.ak[reg & 0x1FU] = value;
}

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

/* A burst transfer fails as a whole when the injected failure falls inside the
 * register range it covers. */
static bool mock_transfer_fails(uint8_t reg, uint16_t len)
{
    if (g_bus.fail_reads) {
        return true;
    }

    return (g_bus.fail_read_reg >= (int)reg) && (g_bus.fail_read_reg < ((int)reg + (int)len));
}

/* Reads auto-increment the register address, like both chips do. */
static uint8_t mock_iic_read(uint8_t addr, uint8_t reg, uint8_t *buf, uint16_t len)
{
    if (mock_transfer_fails(reg, len)) {
        return 1;
    }

    if (addr == g_bus.ak_addr) {
        g_bus.ak_reads++;
        for (uint16_t i = 0; i < len; i++) {
            buf[i] = g_bus.ak[(uint8_t)(reg + i) & 0x1FU];
        }
        return 0;
    }

    if (addr != g_bus.mpu_addr) {
        g_bus.wrong_addr_calls++;
        g_bus.last_wrong_addr = addr;
        return 1; /* nobody on the bus at this address */
    }

    g_bus.mpu_reads++;
    for (uint16_t i = 0; i < len; i++) {
        buf[i] = mock_mpu_reg((uint8_t)(reg + i));
    }

    return 0;
}

static uint8_t mock_iic_write(uint8_t addr, uint8_t reg, uint8_t *buf, uint16_t len)
{
    if (g_bus.fail_writes) {
        return 1;
    }

    if (addr == g_bus.ak_addr) {
        g_bus.ak_writes++;
        for (uint16_t i = 0; i < len; i++) {
            mock_ak_write_reg((uint8_t)(reg + i), buf[i]);
        }
        return 0;
    }

    if (addr != g_bus.mpu_addr) {
        g_bus.wrong_addr_calls++;
        g_bus.last_wrong_addr = addr;
        return 1;
    }

    g_bus.mpu_writes++;
    for (uint16_t i = 0; i < len; i++) {
        mock_mpu_write_reg((uint8_t)(reg + i), buf[i]);
    }

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
 * mpu9250_init() validates every linked pointer before it reads iic_spi. */
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

/* ------------------------------------------------------------------------- */
/* Test fixtures                                                              */
/* ------------------------------------------------------------------------- */

/* Power-on state: an MPU-9250 at 0xD0 with a valid ID and an AK8963 at 0x18. */
static void mock_reset(void)
{
    memset(&g_bus, 0, sizeof(g_bus));

    g_bus.who_am_i = MOCK_MPU_WHO_AM_I;
    g_bus.mpu_addr = MOCK_MPU_ADDR_AD0_LOW;
    g_bus.ak_addr = MOCK_AK_ADDR;
    g_bus.fail_read_reg = -1;

    g_bus.ak[AK_REG_WIA] = MOCK_AK_WIA;
    g_bus.ak[AK_REG_ASAX + 0] = MOCK_ASA_X;
    g_bus.ak[AK_REG_ASAX + 1] = MOCK_ASA_Y;
    g_bus.ak[AK_REG_ASAX + 2] = MOCK_ASA_Z;
}

/* Link the callbacks the way mpu9250_esp_init() does. */
static void link_handle(mpu9250_handle_t *handle, mpu9250_address_t addr_pin)
{
    DRIVER_MPU9250_LINK_INIT(handle, mpu9250_handle_t);
    DRIVER_MPU9250_LINK_IIC_INIT(handle, mock_iic_init);
    DRIVER_MPU9250_LINK_IIC_DEINIT(handle, mock_iic_deinit);
    DRIVER_MPU9250_LINK_IIC_READ(handle, mock_iic_read);
    DRIVER_MPU9250_LINK_IIC_WRITE(handle, mock_iic_write);
    DRIVER_MPU9250_LINK_SPI_INIT(handle, mock_spi_init);
    DRIVER_MPU9250_LINK_SPI_DEINIT(handle, mock_spi_deinit);
    DRIVER_MPU9250_LINK_SPI_READ(handle, mock_spi_read);
    DRIVER_MPU9250_LINK_SPI_WRITE(handle, mock_spi_write);
    DRIVER_MPU9250_LINK_DELAY_MS(handle, mock_delay_ms);
    DRIVER_MPU9250_LINK_DEBUG_PRINT(handle, mock_debug_print);
    DRIVER_MPU9250_LINK_RECEIVE_CALLBACK(handle, mock_receive_callback);

    CHECK(mpu9250_set_interface(handle, MPU9250_INTERFACE_IIC) == 0);
    CHECK(mpu9250_set_addr_pin(handle, addr_pin) == 0);
}

static void mock_set_u16_be(uint8_t reg, uint16_t value)
{
    g_bus.mpu[reg & 0x7FU] = (uint8_t)(value >> 8);
    g_bus.mpu[(uint8_t)(reg + 1) & 0x7FU] = (uint8_t)(value & 0xFFU);
}

/* ACCEL_XOUT_H..GYRO_ZOUT_L (0x3B..0x48) hold six big-endian int16 values with
 * the temperature pair (0x41/0x42) in between; the polling read skips the
 * temperature. */
static void mock_set_motion_sample(const int16_t accel[3], const int16_t gyro[3])
{
    for (unsigned i = 0; i < 3; i++) {
        mock_set_u16_be((uint8_t)(MPU_REG_ACCEL_XOUT_H + 2 * i), (uint16_t)accel[i]);
    }
    for (unsigned i = 0; i < 3; i++) {
        mock_set_u16_be((uint8_t)(MPU_REG_GYRO_XOUT_H + 2 * i), (uint16_t)gyro[i]);
    }
}

/* The AK8963 hands out little-endian data (HXL first) and reports the output
 * resolution in ST2 bit 4, which is what selects the 0.15 vs 0.6 uT/LSB scale. */
static void mock_set_mag_sample(const int16_t mag[3], bool sixteen_bit)
{
    g_bus.ak[AK_REG_ST1] = 0x01; /* data ready */
    for (unsigned i = 0; i < 3; i++) {
        const uint16_t value = (uint16_t)mag[i];
        g_bus.ak[AK_REG_HXL + 2 * i] = (uint8_t)(value & 0xFFU);
        g_bus.ak[AK_REG_HXL + 2 * i + 1] = (uint8_t)(value >> 8);
    }
    g_bus.ak[AK_REG_ST2] = sixteen_bit ? 0x10 : 0x00;
}

/* ------------------------------------------------------------------------- */
/* I2C address selection                                                      */
/* ------------------------------------------------------------------------- */

static void test_i2c_address_selection(void)
{
    mpu9250_handle_t handle;

    /* AD0 low: every MPU transfer goes to 0xD0, the AK8963 to 0x18. */
    mock_reset();
    link_handle(&handle, MPU9250_ADDRESS_AD0_LOW);
    CHECK(mpu9250_init(&handle) == 0);
    CHECK(mpu9250_mag_init(&handle) == 0);
    CHECK(g_bus.mpu_reads > 0);
    CHECK(g_bus.mpu_writes > 0);
    CHECK(g_bus.ak_reads > 0);
    CHECK(g_bus.wrong_addr_calls == 0);

    /* AD0 high against a device wired for AD0 low: the address really is applied
     * to the transfer, so nobody answers and WHO_AM_I cannot be read. */
    mock_reset();
    link_handle(&handle, MPU9250_ADDRESS_AD0_HIGH);
    CHECK(mpu9250_init(&handle) == 5);
    CHECK(g_bus.wrong_addr_calls > 0);
    CHECK(g_bus.last_wrong_addr == MOCK_MPU_ADDR_AD0_HIGH);
    CHECK(handle.inited == 0);

    /* AD0 high against a device wired for AD0 high: 0xD2 is decoded. */
    mock_reset();
    g_bus.mpu_addr = MOCK_MPU_ADDR_AD0_HIGH;
    link_handle(&handle, MPU9250_ADDRESS_AD0_HIGH);
    CHECK(mpu9250_init(&handle) == 0);
    CHECK(g_bus.mpu_reads > 0);
    CHECK(g_bus.wrong_addr_calls == 0);
}

/* ------------------------------------------------------------------------- */
/* MPU-9250 init                                                              */
/* ------------------------------------------------------------------------- */

static void test_init_accepts_valid_device(void)
{
    mpu9250_handle_t handle;

    mock_reset();
    link_handle(&handle, MPU9250_ADDRESS_AD0_LOW);
    CHECK(mpu9250_init(&handle) == 0);
    CHECK(handle.inited == 1);
    CHECK(handle.dmp_inited == 0);
    CHECK(handle.mag_inited == 0); /* the AK8963 needs mpu9250_mag_init() */
    CHECK(g_bus.iic_init_calls == 1);
    CHECK(g_bus.reset_writes == 1); /* DEVICE_RESET written once ... */
    CHECK((g_bus.mpu[MPU_REG_PWR_MGMT_1] & 0x80U) == 0); /* ... and polled until clear */
    CHECK(g_bus.delay_calls == 1); /* the reset settling delay was requested */
}

static void test_init_rejects_wrong_who_am_i(void)
{
    /* Values other than 0x71 must not be mistaken for an MPU-9250. */
    static const uint8_t invalid_ids[] = { 0x00, 0x68, 0x70, 0x74, 0xFF };

    for (unsigned i = 0; i < sizeof(invalid_ids); i++) {
        mpu9250_handle_t handle;

        mock_reset();
        g_bus.who_am_i = invalid_ids[i];
        link_handle(&handle, MPU9250_ADDRESS_AD0_LOW);
        CHECK(mpu9250_init(&handle) == 5);
        CHECK(handle.inited == 0);
        CHECK(g_bus.iic_deinit_calls == 1); /* bus released on the failure path */
        CHECK(g_bus.mpu_writes == 0);       /* nothing configured on a wrong ID */
    }
}

static void test_init_reports_transport_failure(void)
{
    mpu9250_handle_t handle;

    /* The WHO_AM_I read itself fails: status 5 ("read who am i failed"). */
    mock_reset();
    g_bus.fail_reads = true;
    link_handle(&handle, MPU9250_ADDRESS_AD0_LOW);
    CHECK(mpu9250_init(&handle) == 5);
    CHECK(handle.inited == 0);
    CHECK(g_bus.iic_deinit_calls == 1);

    /* The bus cannot be created at all: status 1. */
    mock_reset();
    g_bus.fail_iic_init = true;
    link_handle(&handle, MPU9250_ADDRESS_AD0_LOW);
    CHECK(mpu9250_init(&handle) == 1);
    CHECK(handle.inited == 0);
    CHECK(g_bus.iic_deinit_calls == 0); /* nothing was initialized to tear down */
    CHECK(g_bus.mpu_reads == 0);

    /* The reset write fails: status 4 ("write pwr mgmt 1 failed"). */
    mock_reset();
    g_bus.fail_writes = true;
    link_handle(&handle, MPU9250_ADDRESS_AD0_LOW);
    CHECK(mpu9250_init(&handle) == 4);
    CHECK(handle.inited == 0);
    CHECK(g_bus.iic_deinit_calls == 1);
}

/*
 * The settings mpu9250_esp_init() applies after mpu9250_init(), read back
 * through the getters. mpu9250_init() itself configures nothing, so every value
 * written here differs from the post-reset register state and a dropped write
 * (or a setter that writes the wrong bits) fails these checks.
 */
static void test_applies_adapter_configuration(void)
{
    mpu9250_handle_t handle;

    mock_reset();
    link_handle(&handle, MPU9250_ADDRESS_AD0_LOW);
    CHECK(mpu9250_init(&handle) == 0);

    /* Sleep is toggled both ways: the reset value is already awake. */
    CHECK(mpu9250_set_sleep(&handle, MPU9250_BOOL_TRUE) == 0);
    CHECK((g_bus.mpu[MPU_REG_PWR_MGMT_1] & (1U << 6)) != 0);
    CHECK(mpu9250_set_sleep(&handle, MPU9250_BOOL_FALSE) == 0);
    CHECK((g_bus.mpu[MPU_REG_PWR_MGMT_1] & (1U << 6)) == 0);

    CHECK(mpu9250_set_clock_source(&handle, MPU9250_CLOCK_SOURCE_PLL) == 0);
    CHECK(mpu9250_set_sample_rate_divider(&handle, 19) == 0); /* 1 kHz / 20 = 50 Hz */
    CHECK(mpu9250_set_low_pass_filter(&handle, MPU9250_LOW_PASS_FILTER_3) == 0);
    CHECK(mpu9250_set_accelerometer_range(&handle, MPU9250_ACCELEROMETER_RANGE_16G) == 0);
    CHECK(mpu9250_set_gyroscope_range(&handle, MPU9250_GYROSCOPE_RANGE_250DPS) == 0);

    mpu9250_bool_t sleep = MPU9250_BOOL_TRUE;
    mpu9250_clock_source_t clock = MPU9250_CLOCK_SOURCE_STOP_CLOCK;
    uint8_t divider = 0;
    mpu9250_low_pass_filter_t filter = MPU9250_LOW_PASS_FILTER_0;
    mpu9250_accelerometer_range_t accel_range = MPU9250_ACCELEROMETER_RANGE_2G;
    mpu9250_gyroscope_range_t gyro_range = MPU9250_GYROSCOPE_RANGE_2000DPS;

    CHECK(mpu9250_get_sleep(&handle, &sleep) == 0);
    CHECK(sleep == MPU9250_BOOL_FALSE);
    CHECK(mpu9250_get_clock_source(&handle, &clock) == 0);
    CHECK(clock == MPU9250_CLOCK_SOURCE_PLL);
    CHECK(mpu9250_get_sample_rate_divider(&handle, &divider) == 0);
    CHECK(divider == 19);
    CHECK(mpu9250_get_low_pass_filter(&handle, &filter) == 0);
    CHECK(filter == MPU9250_LOW_PASS_FILTER_3);
    CHECK(mpu9250_get_accelerometer_range(&handle, &accel_range) == 0);
    CHECK(accel_range == MPU9250_ACCELEROMETER_RANGE_16G);
    CHECK(mpu9250_get_gyroscope_range(&handle, &gyro_range) == 0);
    CHECK(gyro_range == MPU9250_GYROSCOPE_RANGE_250DPS);

    /* The range setters own bits 4:3 of the two config registers. */
    CHECK((g_bus.mpu[MPU_REG_ACCEL_CONFIG] >> 3) == MPU9250_ACCELEROMETER_RANGE_16G);
    CHECK((g_bus.mpu[MPU_REG_GYRO_CONFIG] >> 3) == MPU9250_GYROSCOPE_RANGE_250DPS);
}

/* ------------------------------------------------------------------------- */
/* AK8963 init and configuration                                              */
/* ------------------------------------------------------------------------- */

static void test_mag_init_accepts_ak8963(void)
{
    mpu9250_handle_t handle;

    mock_reset();
    link_handle(&handle, MPU9250_ADDRESS_AD0_LOW);
    CHECK(mpu9250_init(&handle) == 0);
    CHECK(mpu9250_mag_init(&handle) == 0);
    CHECK(handle.mag_inited == 1);

    /* The per-axis sensitivity adjustment comes from the AK8963 fuse ROM. */
    CHECK(handle.mag_asa[0] == MOCK_ASA_X);
    CHECK(handle.mag_asa[1] == MOCK_ASA_Y);
    CHECK(handle.mag_asa[2] == MOCK_ASA_Z);

    /* mpu9250_mag_init() disables the I2C master and enables the bypass so the
     * AK8963 answers directly at 0x18. */
    CHECK((g_bus.mpu[MPU_REG_USER_CTRL] & (1U << 5)) == 0);
    CHECK((g_bus.mpu[MPU_REG_INT_PIN_CFG] & (1U << 1)) != 0);
    CHECK(g_bus.ak_reads > 0);
    CHECK(g_bus.ak_writes > 0);
    CHECK(g_bus.wrong_addr_calls == 0);
}

static void test_mag_init_rejects_wrong_wia(void)
{
    static const uint8_t invalid_ids[] = { 0x00, 0x47, 0x49, 0xFF };

    for (unsigned i = 0; i < sizeof(invalid_ids); i++) {
        mpu9250_handle_t handle;

        mock_reset();
        link_handle(&handle, MPU9250_ADDRESS_AD0_LOW);
        CHECK(mpu9250_init(&handle) == 0);
        g_bus.ak[AK_REG_WIA] = invalid_ids[i];
        CHECK(mpu9250_mag_init(&handle) == 5);
        CHECK(handle.mag_inited == 0);
    }
}

static void test_mag_init_reports_read_failure(void)
{
    mpu9250_handle_t handle;

    mock_reset();
    link_handle(&handle, MPU9250_ADDRESS_AD0_LOW);
    CHECK(mpu9250_init(&handle) == 0);

    /* The ASA block read fails: status 1, magnetometer stays unusable. */
    g_bus.fail_read_reg = AK_REG_ASAX;
    CHECK(mpu9250_mag_init(&handle) == 1);
    CHECK(handle.mag_inited == 0);
}

static void test_mag_configuration(void)
{
    mpu9250_handle_t handle;

    mock_reset();
    link_handle(&handle, MPU9250_ADDRESS_AD0_LOW);
    CHECK(mpu9250_init(&handle) == 0);
    CHECK(mpu9250_mag_init(&handle) == 0);

    /* 100 Hz continuous mode 2 (0x06) plus the 16-bit output bit (0x10), the
     * configuration mpu9250_esp_init() applies. */
    CHECK(mpu9250_mag_set_mode(&handle, MPU9250_MAGNETOMETER_MODE_CONTINUOUS2) == 0);
    CHECK(mpu9250_mag_set_bits(&handle, MPU9250_MAGNETOMETER_BITS_16) == 0);
    CHECK(g_bus.ak[AK_REG_CNTL1] == 0x16);

    /* Switching to 14-bit mode must not disturb the measurement mode. */
    CHECK(mpu9250_mag_set_bits(&handle, MPU9250_MAGNETOMETER_BITS_14) == 0);
    CHECK(g_bus.ak[AK_REG_CNTL1] == 0x06);
}

/* ------------------------------------------------------------------------- */
/* Nine-axis read and scaling                                                 */
/* ------------------------------------------------------------------------- */

struct axis_case {
    const char *name;
    mpu9250_accelerometer_range_t accel_range;
    mpu9250_gyroscope_range_t gyro_range;
    int16_t accel_raw[3];
    float accel_g[3];
    int16_t gyro_raw[3];
    float gyro_dps[3];
};

/*
 * Raw counts and the physical values they must produce. The second case changes
 * both ranges, which only works if the driver takes the scale factors from the
 * ACCEL_CONFIG/GYRO_CONFIG registers it reads on every call.
 */
static const struct axis_case axis_cases[] = {
    /* Adapter defaults: +-2 g (16384 LSB/g), +-2000 dps (16.4 LSB/dps). */
    { "2 g / 2000 dps",
      MPU9250_ACCELEROMETER_RANGE_2G,
      MPU9250_GYROSCOPE_RANGE_2000DPS,
      { 16384, -8192, 4096 },
      { 1.0f, -0.5f, 0.25f },
      { 164, -328, 41 },
      { 10.0f, -20.0f, 2.5f } },
    /* +-8 g (4096 LSB/g), +-500 dps (65.5 LSB/dps). */
    { "8 g / 500 dps",
      MPU9250_ACCELEROMETER_RANGE_8G,
      MPU9250_GYROSCOPE_RANGE_500DPS,
      { 8192, -4096, 1024 },
      { 2.0f, -1.0f, 0.25f },
      { 655, -131, 131 },
      { 10.0f, -2.0f, 2.0f } },
    /* Power-on gyro range: +-250 dps (131 LSB/dps). */
    { "2 g / 250 dps",
      MPU9250_ACCELEROMETER_RANGE_2G,
      MPU9250_GYROSCOPE_RANGE_250DPS,
      { -16384, 8192, 0 },
      { -1.0f, 0.5f, 0.0f },
      { 131, -262, 1310 },
      { 1.0f, -2.0f, 10.0f } },
};

/*
 * Microtesla per count is 0.15 for the 16-bit output and 0.6 for the 14-bit one,
 * times the per-axis ASA factor 1.1875 / 1.0 / 0.75 from mock_reset():
 *   16-bit: 400 * 1.1875 * 0.15 = 71.25, -200 * 1.0 * 0.15 = -30, 80 * 0.75 * 0.15 = 9
 *   14-bit: 400 * 1.1875 * 0.6  = 285,   -200 * 1.0 * 0.6  = -120, 80 * 0.75 * 0.6 = 36
 */
static const int16_t mag_sample[3] = { 400, -200, 80 };
static const float mag_sample_16bit_ut[3] = { 71.25f, -30.0f, 9.0f };
static const float mag_sample_14bit_ut[3] = { 285.0f, -120.0f, 36.0f };

static void test_read_nine_axis_scaling(void)
{
    mpu9250_handle_t handle;

    mock_reset();
    link_handle(&handle, MPU9250_ADDRESS_AD0_LOW);
    CHECK(mpu9250_init(&handle) == 0);
    CHECK(mpu9250_mag_init(&handle) == 0);
    CHECK(mpu9250_mag_set_mode(&handle, MPU9250_MAGNETOMETER_MODE_CONTINUOUS2) == 0);
    CHECK(mpu9250_mag_set_bits(&handle, MPU9250_MAGNETOMETER_BITS_16) == 0);

    mock_set_mag_sample(mag_sample, true);

    for (unsigned c = 0; c < sizeof(axis_cases) / sizeof(axis_cases[0]); c++) {
        const struct axis_case *tc = &axis_cases[c];

        CHECK(mpu9250_set_accelerometer_range(&handle, tc->accel_range) == 0);
        CHECK(mpu9250_set_gyroscope_range(&handle, tc->gyro_range) == 0);
        mock_set_motion_sample(tc->accel_raw, tc->gyro_raw);

        int16_t accel_raw[1][3] = { { 0 } };
        int16_t gyro_raw[1][3] = { { 0 } };
        int16_t mag_raw[1][3] = { { 0 } };
        float accel_g[1][3] = { { 0.0f } };
        float gyro_dps[1][3] = { { 0.0f } };
        float mag_ut[1][3] = { { 0.0f } };
        uint16_t len = 1;

        CHECK(mpu9250_read(&handle, accel_raw, accel_g, gyro_raw, gyro_dps, mag_raw, mag_ut,
                           &len) == 0);
        CHECK(len == 1);

        if (len != 1) {
            continue; /* the remaining checks would read unset values */
        }

        for (unsigned axis = 0; axis < 3; axis++) {
            if ((accel_raw[0][axis] != tc->accel_raw[axis]) ||
                (gyro_raw[0][axis] != tc->gyro_raw[axis]) ||
                (mag_raw[0][axis] != mag_sample[axis])) {
                printf("FAIL %s: axis %u raw counts a=%d w=%d m=%d\n", tc->name, axis,
                       accel_raw[0][axis], gyro_raw[0][axis], mag_raw[0][axis]);
                failures++;
            }
            /* The expected values are exact binary quotients, so the tolerance only
             * absorbs float rounding - not a wrong scale factor. */
            CHECK_NEAR(accel_g[0][axis], tc->accel_g[axis], 0.00001f);
            CHECK_NEAR(gyro_dps[0][axis], tc->gyro_dps[axis], 0.00001f);
            CHECK_NEAR(mag_ut[0][axis], mag_sample_16bit_ut[axis], 0.01f);
        }
    }
}

static void test_read_mag_14_bit_scaling(void)
{
    mpu9250_handle_t handle;

    mock_reset();
    link_handle(&handle, MPU9250_ADDRESS_AD0_LOW);
    CHECK(mpu9250_init(&handle) == 0);
    CHECK(mpu9250_mag_init(&handle) == 0);
    CHECK(mpu9250_mag_set_mode(&handle, MPU9250_MAGNETOMETER_MODE_CONTINUOUS2) == 0);
    CHECK(mpu9250_mag_set_bits(&handle, MPU9250_MAGNETOMETER_BITS_14) == 0);

    const int16_t accel[3] = { 0, 0, 0 };
    const int16_t gyro[3] = { 0, 0, 0 };
    mock_set_motion_sample(accel, gyro);
    mock_set_mag_sample(mag_sample, false); /* ST2 bit 4 clear = 14-bit output */

    int16_t accel_raw[1][3] = { { 0 } };
    int16_t gyro_raw[1][3] = { { 0 } };
    int16_t mag_raw[1][3] = { { 0 } };
    float accel_g[1][3] = { { 0.0f } };
    float gyro_dps[1][3] = { { 0.0f } };
    float mag_ut[1][3] = { { 0.0f } };
    uint16_t len = 1;

    CHECK(mpu9250_read(&handle, accel_raw, accel_g, gyro_raw, gyro_dps, mag_raw, mag_ut,
                       &len) == 0);
    CHECK(len == 1);

    for (unsigned axis = 0; axis < 3; axis++) {
        CHECK_NEAR(mag_ut[0][axis], mag_sample_14bit_ut[axis], 0.01f);
    }
}

/* ------------------------------------------------------------------------- */
/* Failure paths and lifecycle                                                */
/* ------------------------------------------------------------------------- */

static void test_read_reports_transport_failure(void)
{
    mpu9250_handle_t handle;

    mock_reset();
    link_handle(&handle, MPU9250_ADDRESS_AD0_LOW);
    CHECK(mpu9250_init(&handle) == 0);
    CHECK(mpu9250_mag_init(&handle) == 0);

    /* The 14-byte accel/gyro/temp block read fails: status 1, which
     * mpu9250_esp_read() maps to ESP_ERR_INVALID_RESPONSE. */
    g_bus.fail_read_reg = MPU_REG_ACCEL_XOUT_H;
    int16_t accel_raw[1][3] = { { 0 } };
    int16_t gyro_raw[1][3] = { { 0 } };
    int16_t mag_raw[1][3] = { { 0 } };
    float accel_g[1][3] = { { 0.0f } };
    float gyro_dps[1][3] = { { 0.0f } };
    float mag_ut[1][3] = { { 0.0f } };
    uint16_t len = 1;

    CHECK(mpu9250_read(&handle, accel_raw, accel_g, gyro_raw, gyro_dps, mag_raw, mag_ut,
                       &len) == 1);

    /* The magnetometer block read fails: same status, no half-filled sample. */
    g_bus.fail_read_reg = AK_REG_HXL;
    CHECK(mpu9250_read(&handle, accel_raw, accel_g, gyro_raw, gyro_dps, mag_raw, mag_ut,
                       &len) == 1);
    CHECK(mag_ut[0][0] == 0.0f);
    CHECK(mag_ut[0][1] == 0.0f);
    CHECK(mag_ut[0][2] == 0.0f);
}

static void test_read_argument_guards(void)
{
    mpu9250_handle_t handle;

    int16_t accel_raw[1][3] = { { 0 } };
    int16_t gyro_raw[1][3] = { { 0 } };
    int16_t mag_raw[1][3] = { { 0 } };
    float accel_g[1][3] = { { 0.0f } };
    float gyro_dps[1][3] = { { 0.0f } };
    float mag_ut[1][3] = { { 0.0f } };
    uint16_t len = 1;

    /* Handle NULL: status 2 (ESP_ERR_INVALID_ARG for the adapter). */
    CHECK(mpu9250_read(NULL, accel_raw, accel_g, gyro_raw, gyro_dps, mag_raw, mag_ut, &len) == 2);

    /* Not initialized: status 3 (ESP_ERR_INVALID_STATE). */
    mock_reset();
    link_handle(&handle, MPU9250_ADDRESS_AD0_LOW);
    CHECK(mpu9250_read(&handle, accel_raw, accel_g, gyro_raw, gyro_dps, mag_raw, mag_ut,
                       &len) == 3);

    /* Zero length: status 4 (ESP_ERR_INVALID_SIZE). */
    CHECK(mpu9250_init(&handle) == 0);
    len = 0;
    CHECK(mpu9250_read(&handle, accel_raw, accel_g, gyro_raw, gyro_dps, mag_raw, mag_ut,
                       &len) == 4);
}

static void test_deinit_powers_down_both_chips(void)
{
    mpu9250_handle_t handle;

    mock_reset();
    link_handle(&handle, MPU9250_ADDRESS_AD0_LOW);
    CHECK(mpu9250_init(&handle) == 0);
    CHECK(mpu9250_mag_init(&handle) == 0);

    /* The adapter's power-down order: AK8963 first, then the MPU. */
    CHECK(mpu9250_mag_deinit(&handle) == 0);
    CHECK(handle.mag_inited == 0);
    CHECK(g_bus.ak[AK_REG_CNTL1] == 0x00); /* power-down mode */

    CHECK(mpu9250_deinit(&handle) == 0);
    CHECK(handle.inited == 0);
    CHECK((g_bus.mpu[MPU_REG_PWR_MGMT_1] & (1U << 6)) != 0); /* sleep */
    CHECK(g_bus.iic_deinit_calls == 1);
}

int main(void)
{
    test_i2c_address_selection();
    test_init_accepts_valid_device();
    test_init_rejects_wrong_who_am_i();
    test_init_reports_transport_failure();
    test_applies_adapter_configuration();
    test_mag_init_accepts_ak8963();
    test_mag_init_rejects_wrong_wia();
    test_mag_init_reports_read_failure();
    test_mag_configuration();
    test_read_nine_axis_scaling();
    test_read_mag_14_bit_scaling();
    test_read_reports_transport_failure();
    test_read_argument_guards();
    test_deinit_powers_down_both_chips();

    if (failures != 0) {
        printf("%d check(s) failed in the MPU-9250 driver host test\n", failures);
        return 1;
    }

    printf("all MPU-9250 driver host checks passed\n");
    return 0;
}
