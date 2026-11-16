/*
 * SPDX-FileCopyrightText: 2024 ECE Balloon Bot Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Register-level MPU-6500 model shared by the two transport-level host tests:
 *
 *   - tests/mpu6500_host_test.c            drives it through the LibDriver
 *                                          callback contract (iic_read/iic_write)
 *                                          that drivers/mpu6500/mpu6500_esp.c
 *                                          implements,
 *   - tests/mpu6500_esp_adapter_host_test.c drives it through the mocked ESP-IDF
 *                                          I2C master API that the adapter calls.
 *
 * Both layers therefore talk to the same chip behavior (auto-incremented burst
 * reads, read-only WHO_AM_I, self-clearing DEVICE_RESET) instead of two drifting
 * fakes. The vendor driver keeps its register map private to
 * src/driver_mpu6500.c, so the registers the tests touch are named here.
 *
 * Everything is `static inline` so the header can be included by more than one
 * translation unit without unused-function warnings.
 */

#pragma once

#include <stdint.h>
#include <string.h>

/* Registers the tests touch (register map of the MPU-6500 datasheet). */
#define MPU6500_MOCK_REG_CONFIG        0x1A
#define MPU6500_MOCK_REG_GYRO_CONFIG   0x1B
#define MPU6500_MOCK_REG_ACCEL_CONFIG  0x1C
#define MPU6500_MOCK_REG_ACCEL_XOUT_H  0x3B
#define MPU6500_MOCK_REG_GYRO_XOUT_H   0x43
#define MPU6500_MOCK_REG_USER_CTRL     0x6A
#define MPU6500_MOCK_REG_PWR_MGMT_1    0x6B
#define MPU6500_MOCK_REG_WHO_AM_I      0x75

#define MPU6500_MOCK_WHO_AM_I_ID       0x70U /* the only ID the MPU-6500 reports */
#define MPU6500_MOCK_WHO_AM_I_MPU9250  0x71U /* what a mis-wired MPU-9250 reports */

/* The register map ends at 0x7E, so a full 128 byte file covers every access. */
#define MPU6500_MOCK_REG_COUNT         128

typedef struct {
    uint8_t reg[MPU6500_MOCK_REG_COUNT]; /* register file, 0x00..0x7F */
    uint8_t who_am_i;                    /* identification register, read-only */
    unsigned reset_writes;               /* DEVICE_RESET writes seen */
    int stuck_reset_register;            /* register that keeps DEVICE_RESET set, -1 = none */
} mpu6500_mock_device_t;

/* Power-on state: an MPU-6500 answers with 0x70, everything else is zero. */
static inline void mpu6500_mock_device_power_on(mpu6500_mock_device_t *dev)
{
    memset(dev, 0, sizeof(*dev));
    dev->who_am_i = MPU6500_MOCK_WHO_AM_I_ID;
    dev->stuck_reset_register = -1;
}

static inline uint8_t mpu6500_mock_device_read_reg(const mpu6500_mock_device_t *dev, uint8_t reg)
{
    reg &= 0x7FU;
    if (reg == MPU6500_MOCK_REG_WHO_AM_I) {
        return dev->who_am_i; /* read-only identification register */
    }
    if ((int)reg == dev->stuck_reset_register) {
        return dev->reg[reg] | 0x80U;
    }

    return dev->reg[reg];
}

/* Writing DEVICE_RESET (PWR_MGMT_1 bit 7) restores the power-on register state
 * and the reset bit itself reads back cleared, so mpu6500_init()'s poll
 * terminates. A device that never clears the bit is modelled by
 * stuck_reset_register, which reproduces the driver's reset timeout.
 */
static inline void mpu6500_mock_device_write_reg(mpu6500_mock_device_t *dev, uint8_t reg,
                                                 uint8_t value)
{
    reg &= 0x7FU;
    if (reg == MPU6500_MOCK_REG_WHO_AM_I) {
        return; /* read-only */
    }

    if ((reg == MPU6500_MOCK_REG_PWR_MGMT_1) && ((value & 0x80U) != 0)) {
        const uint8_t who_am_i = dev->who_am_i;
        const int stuck = dev->stuck_reset_register;

        mpu6500_mock_device_power_on(dev);
        dev->who_am_i = who_am_i;
        dev->stuck_reset_register = stuck;
        dev->reset_writes++;
        return;
    }

    dev->reg[reg] = value;
}

/* Burst reads auto-increment the register address, like the chip does. */
static inline void mpu6500_mock_device_burst_read(const mpu6500_mock_device_t *dev, uint8_t reg,
                                                  uint8_t *buf, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++) {
        buf[i] = mpu6500_mock_device_read_reg(dev, (uint8_t)(reg + i));
    }
}

static inline void mpu6500_mock_device_burst_write(mpu6500_mock_device_t *dev, uint8_t reg,
                                                   const uint8_t *buf, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++) {
        mpu6500_mock_device_write_reg(dev, (uint8_t)(reg + i), buf[i]);
    }
}

/* ACCEL_XOUT_H..GYRO_ZOUT_L (0x3B..0x48) hold six big-endian int16 values with
 * the temperature pair (0x41/0x42) in between; the polling read skips it. */
static inline void mpu6500_mock_device_set_motion(mpu6500_mock_device_t *dev, const int16_t accel[3],
                                                  const int16_t gyro[3])
{
    for (unsigned i = 0; i < 3; i++) {
        const uint16_t a = (uint16_t)accel[i];
        const uint16_t g = (uint16_t)gyro[i];

        dev->reg[MPU6500_MOCK_REG_ACCEL_XOUT_H + 2 * i] = (uint8_t)(a >> 8);
        dev->reg[MPU6500_MOCK_REG_ACCEL_XOUT_H + 2 * i + 1] = (uint8_t)(a & 0xFFU);
        dev->reg[MPU6500_MOCK_REG_GYRO_XOUT_H + 2 * i] = (uint8_t)(g >> 8);
        dev->reg[MPU6500_MOCK_REG_GYRO_XOUT_H + 2 * i + 1] = (uint8_t)(g & 0xFFU);
    }
}
