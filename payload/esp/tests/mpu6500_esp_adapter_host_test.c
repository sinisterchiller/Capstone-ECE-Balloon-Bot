/*
 * SPDX-FileCopyrightText: 2024 ECE Balloon Bot Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Host (no ESP-IDF, no hardware) regression test for the active MPU-6500
 * integration: the unmodified adapter drivers/mpu6500/mpu6500_esp.c running the
 * unmodified vendor driver against a mock ESP-IDF (tests/mpu6500_esp_mock_idf.c).
 *
 * This is the adapter-level companion of tests/mpu6500_host_test.c: instead of
 * driving the LibDriver callbacks by hand, the ESP-IDF I2C master API, esp_timer,
 * FreeRTOS delay and logging are replaced, so the real init preflight, the real
 * error mapping and the real six-axis read path are exercised. It pins down what
 * main/mpu.cpp and the application loop depend on:
 *
 *  1. WHO_AM_I 0x70 is accepted: init returns ESP_OK, the device is reset, the
 *     adapter's configuration (+-2 g, +-2000 dps, 50 Hz, DLPF 3, PLL, awake) is
 *     in the registers, and the bus/device stay open.
 *  2. WHO_AM_I 0x71 (an MPU-9250, what the previous integration accepted) and
 *     other IDs are rejected with ESP_ERR_NOT_FOUND before the vendor driver
 *     configures anything, and the bus is released again.
 *  3. An I2C failure on the WHO_AM_I read is reported as
 *     ESP_ERR_INVALID_RESPONSE, a bus that cannot be created as the failing
 *     code, and a sensor that stops answering a sample read as
 *     ESP_ERR_INVALID_RESPONSE - never as a valid sample.
 *  4. A successful read returns both axis triplets scaled from the configured
 *     ranges (+-2 g: 16384 LSB/g, +-2000 dps: 16.4 LSB/dps) and the
 *     esp_timer timestamp. There is no magnetometer reading: the MPU-6500 is a
 *     six-axis part.
 *  5. Argument guards, power-down, and that the adapter is reusable after
 *     deinit (its single-device slot is released).
 *
 * Build and run with tests/run_mpu6500_host_test.sh.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "mpu6500_esp.h"
#include "mpu6500_esp_mock_idf.h"
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

/* ------------------------------------------------------------------------- */
/* Init                                                                      */
/* ------------------------------------------------------------------------- */

static void test_init_accepts_mpu6500(void)
{
    mpu6500_mock_i2c_reset();
    mpu6500_mock_i2c_stats_t stats;

    mpu6500_esp_handle_t handle = NULL;
    CHECK(mpu6500_esp_init(&handle) == ESP_OK);
    CHECK(handle != NULL);

    mpu6500_mock_i2c_get_stats(&stats);
    CHECK(stats.bus_creates == 1);
    CHECK(stats.device_adds == 1);
    CHECK(stats.bus_present);
    CHECK(stats.device_present);
    CHECK(stats.bus_deletes == 0);

    /* The vendor driver reset the device and waited for the reset to clear. */
    const mpu6500_mock_device_t *chip = mpu6500_mock_i2c_chip();
    CHECK(chip->reset_writes == 1);
    CHECK((chip->reg[MPU6500_MOCK_REG_PWR_MGMT_1] & 0x80U) == 0);
    CHECK(stats.delay_calls == 1);

    /* The adapter's configuration: awake, PLL clock, 50 Hz, DLPF 3, +-2 g and
     * +-2000 dps. */
    CHECK((chip->reg[MPU6500_MOCK_REG_PWR_MGMT_1] & (1U << 6)) == 0);
    CHECK((chip->reg[MPU6500_MOCK_REG_PWR_MGMT_1] & 0x07U) == 0x01U);
    CHECK(chip->reg[0x19] == 19); /* SMPRT_DIV: 1 kHz / (19 + 1) = 50 Hz */
    CHECK((chip->reg[MPU6500_MOCK_REG_CONFIG] & 0x07U) == 0x03U);
    CHECK((chip->reg[MPU6500_MOCK_REG_ACCEL_CONFIG] >> 3) == 0x00U); /* the +-2 g value */
    CHECK((chip->reg[MPU6500_MOCK_REG_GYRO_CONFIG] >> 3) == 0x03U);  /* the +-2000 dps value */

    CHECK(mpu6500_esp_deinit(handle) == ESP_OK);
}

static void test_init_rejects_wrong_who_am_i(void)
{
    /* 0x71 is the MPU-9250 ID this integration replaced; the MPU-6500 only ever
     * reports 0x70. */
    static const uint8_t invalid_ids[] = {
        MPU6500_MOCK_WHO_AM_I_MPU9250, 0x00, 0x68, 0xFF
    };

    for (unsigned i = 0; i < sizeof(invalid_ids); i++) {
        mpu6500_mock_i2c_reset();
        mpu6500_mock_i2c_chip()->who_am_i = invalid_ids[i];

        mpu6500_esp_handle_t handle = NULL;
        CHECK(mpu6500_esp_init(&handle) == ESP_ERR_NOT_FOUND);
        CHECK(handle == NULL);

        /* Rejected by the preflight, so the vendor driver never configured
         * anything and the bus was released again. */
        const mpu6500_mock_device_t *chip = mpu6500_mock_i2c_chip();
        CHECK(chip->reset_writes == 0);

        mpu6500_mock_i2c_stats_t stats;
        mpu6500_mock_i2c_get_stats(&stats);
        CHECK(!stats.bus_present);
        CHECK(!stats.device_present);
        CHECK(stats.device_removes == 1);
        CHECK(stats.bus_deletes == 1);
    }
}

static void test_init_reports_i2c_failure(void)
{
    /* The WHO_AM_I transfer itself fails: the sensor did not answer, which is a
     * different diagnosis from a wrong ID. */
    mpu6500_mock_i2c_reset();
    mpu6500_mock_i2c_fail_reads_of_register(MPU6500_MOCK_REG_WHO_AM_I);

    mpu6500_esp_handle_t handle = NULL;
    CHECK(mpu6500_esp_init(&handle) == ESP_ERR_INVALID_RESPONSE);
    CHECK(handle == NULL);

    mpu6500_mock_i2c_stats_t stats;
    mpu6500_mock_i2c_get_stats(&stats);
    CHECK(!stats.bus_present);
    CHECK(stats.bus_deletes == 1);

    /* The bus cannot be created at all: the adapter forwards that code. */
    mpu6500_mock_i2c_reset();
    mpu6500_mock_i2c_fail_bus_create(ESP_ERR_INVALID_STATE);

    handle = NULL;
    CHECK(mpu6500_esp_init(&handle) == ESP_ERR_INVALID_STATE);
    CHECK(handle == NULL);

    mpu6500_mock_i2c_get_stats(&stats);
    CHECK(stats.bus_creates == 0);
    CHECK(stats.device_adds == 0);

    /* A rejected handle is not a bus failure. */
    CHECK(mpu6500_esp_init(NULL) == ESP_ERR_INVALID_ARG);
}

/* ------------------------------------------------------------------------- */
/* Six-axis read                                                             */
/* ------------------------------------------------------------------------- */

static void test_read_returns_scaled_six_axis_sample(void)
{
    mpu6500_mock_i2c_reset();

    mpu6500_esp_handle_t handle = NULL;
    CHECK(mpu6500_esp_init(&handle) == ESP_OK);

    /* +-2 g is 16384 LSB/g and +-2000 dps is 16.4 LSB/dps, so these counts must
     * come back as the exact values the application logs. */
    const int16_t accel[3] = { 16384, -8192, 4096 };
    const int16_t gyro[3] = { 164, -328, 41 };
    mpu6500_mock_device_set_motion(mpu6500_mock_i2c_chip(), accel, gyro);
    mpu6500_mock_i2c_set_time_us(123456789);

    mpu6500_esp_reading_t reading;
    memset(&reading, 0xAA, sizeof(reading)); /* the adapter must fill every field */
    CHECK(mpu6500_esp_read(handle, &reading) == ESP_OK);

    CHECK_NEAR(reading.accel_g[0], 1.0f, 0.00001f);
    CHECK_NEAR(reading.accel_g[1], -0.5f, 0.00001f);
    CHECK_NEAR(reading.accel_g[2], 0.25f, 0.00001f);
    CHECK_NEAR(reading.gyro_dps[0], 10.0f, 0.00001f);
    CHECK_NEAR(reading.gyro_dps[1], -20.0f, 0.00001f);
    CHECK_NEAR(reading.gyro_dps[2], 2.5f, 0.00001f);
    CHECK(reading.timestamp_us == 123456789);

    /* A second sample with the sensor moved is not stale data. */
    const int16_t accel2[3] = { 0, 0, -16384 };
    const int16_t gyro2[3] = { -164, 0, 164 };
    mpu6500_mock_device_set_motion(mpu6500_mock_i2c_chip(), accel2, gyro2);
    mpu6500_mock_i2c_set_time_us(222222222);

    CHECK(mpu6500_esp_read(handle, &reading) == ESP_OK);
    CHECK_NEAR(reading.accel_g[2], -1.0f, 0.00001f);
    CHECK_NEAR(reading.gyro_dps[0], -10.0f, 0.00001f);
    CHECK_NEAR(reading.gyro_dps[1], 0.0f, 0.00001f);
    CHECK_NEAR(reading.gyro_dps[2], 10.0f, 0.00001f);
    CHECK(reading.timestamp_us == 222222222);

    CHECK(mpu6500_esp_deinit(handle) == ESP_OK);
}

static void test_read_reports_transport_failure(void)
{
    mpu6500_mock_i2c_reset();

    mpu6500_esp_handle_t handle = NULL;
    CHECK(mpu6500_esp_init(&handle) == ESP_OK);

    /* The 14-byte accel/gyro block read fails: the sample is reported as failed
     * and the output stays zeroed rather than carrying a half-filled reading. */
    mpu6500_mock_i2c_fail_reads_of_register(MPU6500_MOCK_REG_ACCEL_XOUT_H);

    mpu6500_esp_reading_t reading;
    memset(&reading, 0xAA, sizeof(reading));
    CHECK(mpu6500_esp_read(handle, &reading) == ESP_ERR_INVALID_RESPONSE);
    CHECK(reading.accel_g[0] == 0.0f);
    CHECK(reading.accel_g[1] == 0.0f);
    CHECK(reading.accel_g[2] == 0.0f);
    CHECK(reading.gyro_dps[0] == 0.0f);
    CHECK(reading.gyro_dps[1] == 0.0f);
    CHECK(reading.gyro_dps[2] == 0.0f);
    CHECK(reading.timestamp_us == 0);

    /* The sensor answers again once the fault is gone. */
    mpu6500_mock_i2c_fail_reads_of_register(-1);
    CHECK(mpu6500_esp_read(handle, &reading) == ESP_OK);

    CHECK(mpu6500_esp_deinit(handle) == ESP_OK);
}

static void test_read_argument_guards(void)
{
    mpu6500_mock_i2c_reset();

    mpu6500_esp_handle_t handle = NULL;
    CHECK(mpu6500_esp_init(&handle) == ESP_OK);

    mpu6500_esp_reading_t reading = { 0 };
    CHECK(mpu6500_esp_read(NULL, &reading) == ESP_ERR_INVALID_ARG);
    CHECK(mpu6500_esp_read(handle, NULL) == ESP_ERR_INVALID_ARG);

    CHECK(mpu6500_esp_deinit(handle) == ESP_OK);
}

/* ------------------------------------------------------------------------- */
/* Power down and reuse                                                      */
/* ------------------------------------------------------------------------- */

static void test_deinit_powers_down_and_releases(void)
{
    mpu6500_mock_i2c_reset();

    mpu6500_esp_handle_t handle = NULL;
    CHECK(mpu6500_esp_init(&handle) == ESP_OK);
    CHECK(mpu6500_esp_deinit(handle) == ESP_OK);

    /* Sleep mode with the clock stopped, then device and bus released. */
    const mpu6500_mock_device_t *chip = mpu6500_mock_i2c_chip();
    CHECK((chip->reg[MPU6500_MOCK_REG_PWR_MGMT_1] & (1U << 6)) != 0);
    CHECK((chip->reg[MPU6500_MOCK_REG_PWR_MGMT_1] & 0x07U) == 0x07U);

    mpu6500_mock_i2c_stats_t stats;
    mpu6500_mock_i2c_get_stats(&stats);
    CHECK(!stats.bus_present);
    CHECK(!stats.device_present);
    CHECK(stats.device_removes == 1);
    CHECK(stats.bus_deletes == 1);

    /* The single-device slot is free again, so the adapter can be initialized a
     * second time (app restart after a sensor fault). */
    mpu6500_esp_handle_t again = NULL;
    CHECK(mpu6500_esp_init(&again) == ESP_OK);
    CHECK(again != NULL);
    CHECK(mpu6500_esp_deinit(again) == ESP_OK);

    /* A NULL handle is rejected; a released one is not reused by the test. */
    CHECK(mpu6500_esp_deinit(NULL) == ESP_ERR_INVALID_ARG);
}

int main(void)
{
    test_init_accepts_mpu6500();
    test_init_rejects_wrong_who_am_i();
    test_init_reports_i2c_failure();
    test_read_returns_scaled_six_axis_sample();
    test_read_reports_transport_failure();
    test_read_argument_guards();
    test_deinit_powers_down_and_releases();

    if (failures != 0) {
        printf("%d check(s) failed in the MPU-6500 adapter host test\n", failures);
        return 1;
    }

    printf("all MPU-6500 adapter host checks passed\n");
    return 0;
}
