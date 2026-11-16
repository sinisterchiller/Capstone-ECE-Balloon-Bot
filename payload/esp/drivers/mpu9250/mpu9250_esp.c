/*
 * SPDX-FileCopyrightText: 2024 ECE Balloon Bot Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ESP-IDF I2C adapter for the LibDriver MPU-9250 driver (MPU-9250 + AK8963).
 *
 * Vendor source: libdriver/mpu9250 v1.0.11,
 * commit 06cc13e8d6f69e00e7a95f31e5794ce72d77e145 (see README.md).
 * Vendor files (src/driver_mpu9250.c, src/driver_mpu9250.h,
 * src/driver_mpu9250_code.h, LICENSE) are unmodified.
 */

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/i2c_master.h"

#include "driver_mpu9250.h"
#include "mpu9250_esp.h"

static const char *TAG = "mpu9250_esp";

/* Fixed wiring (task/task.md): SDA GPIO21, SCL GPIO22, 100 kHz, AD0 tied to GND. */
#define MPU9250_ESP_I2C_PORT          I2C_NUM_0
#define MPU9250_ESP_SDA_GPIO          21
#define MPU9250_ESP_SCL_GPIO          22
#define MPU9250_ESP_I2C_CLOCK_HZ      100000
#define MPU9250_ESP_MPU_ADDR_7BIT     0x68
#define MPU9250_ESP_AK8963_ADDR_7BIT  0x0C /* reached through the MPU's I2C bypass */

/* Identification register the upstream mpu9250_init() reads and validates. */
#define MPU9250_ESP_REG_WHO_AM_I      0x75
#define MPU9250_ESP_WHO_AM_I_ID       0x71 /* MPU-9250 (both die revisions) */

/* Per-transaction timeout; a 100 kHz byte takes ~90 us, a 14-byte read ~1.3 ms */
#define MPU9250_ESP_I2C_TIMEOUT_MS    100

/* Register address byte plus payload of one upstream write. The largest write the
 * polling paths issue is 1 + 16 bytes (DMP memory load); FIFO reads are reads. */
#define MPU9250_ESP_WRITE_MAX_BYTES   64

/* Sample rate the upstream basic example configures: 1 kHz / (divider + 1) */
#define MPU9250_ESP_SAMPLE_RATE_HZ    50

struct mpu9250_esp_dev_t {
    mpu9250_handle_t handle;
    i2c_master_bus_handle_t bus;
    i2c_master_dev_handle_t mpu_dev;
    i2c_master_dev_handle_t ak_dev;
    bool initialized;
};

/*
 * The LibDriver callbacks are plain function pointers without a context argument,
 * so the adapter keeps the single active device here. mpu9250_esp_init() refuses a
 * second device while this is set; mpu9250_esp_release() clears it.
 */
static struct mpu9250_esp_dev_t *s_ctx;

/* Upstream passes the 8-bit I2C write address: 0xD0 for the MPU (AD0 low),
 * 0x18 for the AK8963 (0x0C << 1). */
static i2c_master_dev_handle_t mpu9250_esp_dev_for_addr(const struct mpu9250_esp_dev_t *ctx,
                                                        uint8_t addr)
{
    switch (addr >> 1) {
        case MPU9250_ESP_MPU_ADDR_7BIT:
            return ctx->mpu_dev;
        case MPU9250_ESP_AK8963_ADDR_7BIT:
            return ctx->ak_dev;
        default:
            return NULL;
    }
}

/* Remove both device handles and delete the bus. Idempotent: the upstream driver
 * calls the iic_deinit hook from mpu9250_deinit() and from some init failure
 * paths, so neither this function nor mpu9250_esp_release() may free twice. */
static void mpu9250_esp_release_bus(struct mpu9250_esp_dev_t *ctx)
{
    if (ctx->ak_dev != NULL) {
        i2c_master_bus_rm_device(ctx->ak_dev);
        ctx->ak_dev = NULL;
    }
    if (ctx->mpu_dev != NULL) {
        i2c_master_bus_rm_device(ctx->mpu_dev);
        ctx->mpu_dev = NULL;
    }
    if (ctx->bus != NULL) {
        i2c_del_master_bus(ctx->bus);
        ctx->bus = NULL;
    }
}

static void mpu9250_esp_release(struct mpu9250_esp_dev_t *ctx)
{
    mpu9250_esp_release_bus(ctx);
    s_ctx = NULL;
    free(ctx);
}

/*
 * LibDriver I2C callback contract (see a_mpu9250_read()/a_mpu9250_write() in
 * driver_mpu9250.c): addr is the 8-bit write address, reg the register to access
 * and the data transfer is a register write followed by a repeated-start read for
 * reads, or one register+data write for writes.
 *
 * None of the callbacks test the "initialized" flag: mpu9250_init() issues reads
 * and writes before init has completed, so the bus and device handles are the only
 * preconditions.
 */
static uint8_t mpu9250_esp_iic_read(uint8_t addr, uint8_t reg, uint8_t *buf, uint16_t len)
{
    struct mpu9250_esp_dev_t *ctx = s_ctx;

    if (ctx == NULL) {
        return 1;
    }
    if ((buf == NULL) && (len != 0)) {
        ESP_LOGE(TAG, "I2C read with a NULL buffer");
        return 1;
    }
    if (len == 0) {
        return 0;
    }

    i2c_master_dev_handle_t dev = mpu9250_esp_dev_for_addr(ctx, addr);
    if (dev == NULL) {
        ESP_LOGE(TAG, "I2C read from unsupported address 0x%02X", (unsigned)addr);
        return 1;
    }

    esp_err_t err = i2c_master_transmit_receive(dev, &reg, 1, buf, len, MPU9250_ESP_I2C_TIMEOUT_MS);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "I2C read of reg 0x%02X from 0x%02X failed: %s",
                 (unsigned)reg, (unsigned)(addr >> 1), esp_err_to_name(err));
        return 1;
    }

    return 0;
}

static uint8_t mpu9250_esp_iic_write(uint8_t addr, uint8_t reg, uint8_t *buf, uint16_t len)
{
    struct mpu9250_esp_dev_t *ctx = s_ctx;

    if (ctx == NULL) {
        return 1;
    }
    if ((buf == NULL) && (len != 0)) {
        ESP_LOGE(TAG, "I2C write with a NULL buffer");
        return 1;
    }
    if (len > MPU9250_ESP_WRITE_MAX_BYTES) {
        ESP_LOGE(TAG, "I2C write of %u bytes exceeds the %d byte adapter limit",
                 (unsigned)len, MPU9250_ESP_WRITE_MAX_BYTES);
        return 1;
    }

    i2c_master_dev_handle_t dev = mpu9250_esp_dev_for_addr(ctx, addr);
    if (dev == NULL) {
        ESP_LOGE(TAG, "I2C write to unsupported address 0x%02X", (unsigned)addr);
        return 1;
    }

    /* One transaction: the register address and its payload must not be split by a
     * stop condition. */
    uint8_t frame[1 + MPU9250_ESP_WRITE_MAX_BYTES];
    frame[0] = reg;
    if (len != 0) {
        memcpy(&frame[1], buf, len);
    }

    esp_err_t err = i2c_master_transmit(dev, frame, (size_t)len + 1, MPU9250_ESP_I2C_TIMEOUT_MS);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "I2C write of reg 0x%02X to 0x%02X failed: %s",
                 (unsigned)reg, (unsigned)(addr >> 1), esp_err_to_name(err));
        return 1;
    }

    return 0;
}

/*
 * Report the identification register before the upstream init runs. The upstream
 * mpu9250_init() collapses "transfer failed" and "wrong chip" into the same
 * status 5 and only says "id is invalid", which does not say whether the sensor
 * answered. This preflight reads the same register through the same I2C device
 * handle the callbacks use and logs either the transport error or the observed
 * value. It never accepts an ID other than 0x71: the upstream check in
 * mpu9250_init() still runs and remains the authority on identification.
 */
static esp_err_t mpu9250_esp_check_who_am_i(const struct mpu9250_esp_dev_t *ctx)
{
    uint8_t reg = MPU9250_ESP_REG_WHO_AM_I;
    uint8_t id = 0;

    esp_err_t err = i2c_master_transmit_receive(ctx->mpu_dev, &reg, 1, &id, 1,
                                                MPU9250_ESP_I2C_TIMEOUT_MS);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "WHO_AM_I (0x%02X) read from 0x%02X failed: %s",
                 (unsigned)MPU9250_ESP_REG_WHO_AM_I, (unsigned)MPU9250_ESP_MPU_ADDR_7BIT,
                 esp_err_to_name(err));
        return ESP_ERR_INVALID_RESPONSE;
    }

    if (id != MPU9250_ESP_WHO_AM_I_ID) {
        ESP_LOGE(TAG, "WHO_AM_I (0x%02X) at 0x%02X returned 0x%02X, expected 0x%02X",
                 (unsigned)MPU9250_ESP_REG_WHO_AM_I, (unsigned)MPU9250_ESP_MPU_ADDR_7BIT,
                 (unsigned)id, (unsigned)MPU9250_ESP_WHO_AM_I_ID);
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG, "WHO_AM_I (0x%02X) at 0x%02X = 0x%02X",
             (unsigned)MPU9250_ESP_REG_WHO_AM_I, (unsigned)MPU9250_ESP_MPU_ADDR_7BIT,
             (unsigned)id);
    return ESP_OK;
}

/* The adapter owns the bus, so the upstream "iic init" hook has nothing to do
 * beyond reporting that the bus it is about to use exists. */
static uint8_t mpu9250_esp_iic_init(void)
{
    return ((s_ctx != NULL) && (s_ctx->bus != NULL)) ? 0 : 1;
}

/* Called by mpu9250_deinit() and by the failure paths of mpu9250_init(): the
 * upstream driver expects the bus to be gone when this returns 0. */
static uint8_t mpu9250_esp_iic_deinit(void)
{
    if (s_ctx == NULL) {
        return 1;
    }

    mpu9250_esp_release_bus(s_ctx);
    return 0;
}

/*
 * This integration is I2C-only, but mpu9250_init() validates every linked pointer
 * (SPI included) before it looks at the selected interface, so the SPI hooks are
 * linked to stubs that report failure if they are ever reached.
 */
static uint8_t mpu9250_esp_spi_init(void)
{
    ESP_LOGE(TAG, "SPI is not supported by this integration");
    return 1;
}

static uint8_t mpu9250_esp_spi_deinit(void)
{
    return 1;
}

static uint8_t mpu9250_esp_spi_read(uint8_t reg, uint8_t *buf, uint16_t len)
{
    (void)reg;
    (void)buf;
    (void)len;
    return 1;
}

static uint8_t mpu9250_esp_spi_write(uint8_t reg, uint8_t *buf, uint16_t len)
{
    (void)reg;
    (void)buf;
    (void)len;
    return 1;
}

/* Polling only: no interrupt pin is wired, so the data-ready callback is unused. */
static void mpu9250_esp_receive_callback(uint8_t type)
{
    (void)type;
}

/* The sensor is always driven from task context */
static void mpu9250_esp_delay_ms(uint32_t ms)
{
    if (ms == 0) {
        return;
    }

    TickType_t ticks = pdMS_TO_TICKS(ms);
    vTaskDelay(ticks > 0 ? ticks : 1);
}

/* The upstream driver logs failure descriptions, each already ending in "\n" */
static void mpu9250_esp_debug_print(const char *const fmt, ...)
{
    char line[160];
    va_list args;

    va_start(args, fmt);
    vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);

    size_t len = strlen(line);
    while ((len > 0) && ((line[len - 1] == '\n') || (line[len - 1] == '\r'))) {
        line[--len] = '\0';
    }
    if (len > 0) {
        ESP_LOGE(TAG, "%s", line);
    }
}

/* mpu9250_init() status codes: 1 bus init, 2 handle NULL, 3 linked functions NULL,
 * 4 reset failed, 5 WHO_AM_I read failed or wrong id */
static esp_err_t mpu9250_esp_err_from_init(uint8_t rslt)
{
    switch (rslt) {
        case 0:
            return ESP_OK;
        case 1:
            return ESP_FAIL;
        case 2:
            return ESP_ERR_INVALID_ARG;
        case 3:
            return ESP_ERR_INVALID_STATE;
        case 4:
            return ESP_FAIL;
        case 5:
            return ESP_ERR_NOT_FOUND;
        default:
            return ESP_FAIL;
    }
}

/* mpu9250_mag_init() status codes: 1 mag init, 2 handle NULL, 3 not initialized,
 * 4 reset failed, 5 AK8963 WIA invalid, 6 SPI not supported */
static esp_err_t mpu9250_esp_err_from_mag_init(uint8_t rslt)
{
    switch (rslt) {
        case 0:
            return ESP_OK;
        case 1:
            return ESP_FAIL;
        case 2:
            return ESP_ERR_INVALID_ARG;
        case 3:
            return ESP_ERR_INVALID_STATE;
        case 4:
            return ESP_FAIL;
        case 5:
            return ESP_ERR_NOT_FOUND;
        case 6:
            return ESP_ERR_NOT_SUPPORTED;
        default:
            return ESP_FAIL;
    }
}

/* Configuration setter status codes: 1 register access, 2 handle NULL, 3 not
 * initialized, 4 magnetometer not initialized (AK8963 setters only) */
static esp_err_t mpu9250_esp_err_from_setter(uint8_t rslt)
{
    switch (rslt) {
        case 0:
            return ESP_OK;
        case 1:
            return ESP_FAIL;
        case 2:
            return ESP_ERR_INVALID_ARG;
        case 3:
        case 4:
            return ESP_ERR_INVALID_STATE;
        default:
            return ESP_FAIL;
    }
}

/* mpu9250_read() status codes: 1 read failed, 2 handle NULL, 3 not initialized,
 * 4 length zero, 5 DMP running, 6 FIFO configuration error */
static esp_err_t mpu9250_esp_err_from_read(uint8_t rslt)
{
    switch (rslt) {
        case 0:
            return ESP_OK;
        case 1:
            return ESP_ERR_INVALID_RESPONSE;
        case 2:
            return ESP_ERR_INVALID_ARG;
        case 3:
        case 5:
        case 6:
            return ESP_ERR_INVALID_STATE;
        case 4:
            return ESP_ERR_INVALID_SIZE;
        default:
            return ESP_FAIL;
    }
}

/* Every configuration setter follows the same failure shape: log which call failed,
 * translate the status code and unwind through the init cleanup path. */
#define MPU9250_ESP_APPLY(call, name)                                        \
    do {                                                                     \
        uint8_t rslt_ = (call);                                              \
        if (rslt_ != 0) {                                                    \
            ESP_LOGE(TAG, "mpu9250_%s failed (rslt=%u)", (name),             \
                     (unsigned)rslt_);                                       \
            err = mpu9250_esp_err_from_setter(rslt_);                        \
            goto fail;                                                       \
        }                                                                    \
    } while (0)

esp_err_t mpu9250_esp_init(mpu9250_esp_handle_t *out_handle)
{
    ESP_RETURN_ON_FALSE(out_handle != NULL, ESP_ERR_INVALID_ARG, TAG, "Null handle");
    *out_handle = NULL;

    ESP_RETURN_ON_FALSE(s_ctx == NULL, ESP_ERR_INVALID_STATE, TAG,
                        "MPU-9250 already initialized (one device at a time)");

    struct mpu9250_esp_dev_t *ctx = calloc(1, sizeof(*ctx));
    ESP_RETURN_ON_FALSE(ctx != NULL, ESP_ERR_NO_MEM, TAG, "Failed to allocate device context");

    /* The upstream callbacks resolve the bus through this pointer, so it is set
     * before the first upstream call and cleared by mpu9250_esp_release(). */
    s_ctx = ctx;

    esp_err_t err;

    const i2c_master_bus_config_t bus_config = {
        .i2c_port = MPU9250_ESP_I2C_PORT,
        .sda_io_num = MPU9250_ESP_SDA_GPIO,
        .scl_io_num = MPU9250_ESP_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = 1,
    };

    err = i2c_new_master_bus(&bus_config, &ctx->bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C bus init failed: %s", esp_err_to_name(err));
        goto fail;
    }

    const i2c_device_config_t mpu_dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = MPU9250_ESP_MPU_ADDR_7BIT,
        .scl_speed_hz = MPU9250_ESP_I2C_CLOCK_HZ,
    };

    err = i2c_master_bus_add_device(ctx->bus, &mpu_dev_config, &ctx->mpu_dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C device add (0x%02X) failed: %s",
                 (unsigned)MPU9250_ESP_MPU_ADDR_7BIT, esp_err_to_name(err));
        goto fail;
    }

    const i2c_device_config_t ak_dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = MPU9250_ESP_AK8963_ADDR_7BIT,
        .scl_speed_hz = MPU9250_ESP_I2C_CLOCK_HZ,
    };

    err = i2c_master_bus_add_device(ctx->bus, &ak_dev_config, &ctx->ak_dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C device add (0x%02X) failed: %s",
                 (unsigned)MPU9250_ESP_AK8963_ADDR_7BIT, esp_err_to_name(err));
        goto fail;
    }

    err = mpu9250_esp_check_who_am_i(ctx);
    if (err != ESP_OK) {
        goto fail;
    }

    DRIVER_MPU9250_LINK_INIT(&ctx->handle, mpu9250_handle_t);
    DRIVER_MPU9250_LINK_IIC_INIT(&ctx->handle, mpu9250_esp_iic_init);
    DRIVER_MPU9250_LINK_IIC_DEINIT(&ctx->handle, mpu9250_esp_iic_deinit);
    DRIVER_MPU9250_LINK_IIC_READ(&ctx->handle, mpu9250_esp_iic_read);
    DRIVER_MPU9250_LINK_IIC_WRITE(&ctx->handle, mpu9250_esp_iic_write);
    DRIVER_MPU9250_LINK_SPI_INIT(&ctx->handle, mpu9250_esp_spi_init);
    DRIVER_MPU9250_LINK_SPI_DEINIT(&ctx->handle, mpu9250_esp_spi_deinit);
    DRIVER_MPU9250_LINK_SPI_READ(&ctx->handle, mpu9250_esp_spi_read);
    DRIVER_MPU9250_LINK_SPI_WRITE(&ctx->handle, mpu9250_esp_spi_write);
    DRIVER_MPU9250_LINK_DELAY_MS(&ctx->handle, mpu9250_esp_delay_ms);
    DRIVER_MPU9250_LINK_DEBUG_PRINT(&ctx->handle, mpu9250_esp_debug_print);
    DRIVER_MPU9250_LINK_RECEIVE_CALLBACK(&ctx->handle, mpu9250_esp_receive_callback);

    MPU9250_ESP_APPLY(mpu9250_set_interface(&ctx->handle, MPU9250_INTERFACE_IIC), "set_interface");
    MPU9250_ESP_APPLY(mpu9250_set_addr_pin(&ctx->handle, MPU9250_ADDRESS_AD0_LOW), "set_addr_pin");

    uint8_t rslt = mpu9250_init(&ctx->handle);
    if (rslt != 0) {
        /* mpu9250_init() deinitializes the bus itself on most failure paths; the
         * cleanup below tolerates a bus that is already gone. The WHO_AM_I value
         * (or the transport error) was already logged by the preflight above. */
        ESP_LOGE(TAG, "MPU-9250 init failed (rslt=%u); bus I2C%d SDA=GPIO%d SCL=GPIO%d %d Hz, "
                      "address 0x%02X (AD0 low), WHO_AM_I (0x75) must return 0x71",
                 (unsigned)rslt, MPU9250_ESP_I2C_PORT, MPU9250_ESP_SDA_GPIO,
                 MPU9250_ESP_SCL_GPIO, MPU9250_ESP_I2C_CLOCK_HZ,
                 (unsigned)MPU9250_ESP_MPU_ADDR_7BIT);
        err = mpu9250_esp_err_from_init(rslt);
        goto fail;
    }

    /* The reset leaves the device asleep; the remaining settings follow the
     * upstream basic example (50 Hz, PLL clock, DLPF 3, +-2 g, +-2000 dps). */
    MPU9250_ESP_APPLY(mpu9250_set_sleep(&ctx->handle, MPU9250_BOOL_FALSE), "set_sleep");
    MPU9250_ESP_APPLY(mpu9250_set_clock_source(&ctx->handle, MPU9250_CLOCK_SOURCE_PLL),
                      "set_clock_source");
    MPU9250_ESP_APPLY(mpu9250_set_sample_rate_divider(&ctx->handle,
                                                      (1000 / MPU9250_ESP_SAMPLE_RATE_HZ) - 1),
                      "set_sample_rate_divider");
    MPU9250_ESP_APPLY(mpu9250_set_low_pass_filter(&ctx->handle, MPU9250_LOW_PASS_FILTER_3),
                      "set_low_pass_filter");
    MPU9250_ESP_APPLY(mpu9250_set_accelerometer_range(&ctx->handle,
                                                      MPU9250_ACCELEROMETER_RANGE_2G),
                      "set_accelerometer_range");
    MPU9250_ESP_APPLY(mpu9250_set_gyroscope_range(&ctx->handle, MPU9250_GYROSCOPE_RANGE_2000DPS),
                      "set_gyroscope_range");

    /* AK8963: enables the MPU I2C bypass, resets the magnetometer and reads its
     * fuse-ROM sensitivity adjustment. */
    rslt = mpu9250_mag_init(&ctx->handle);
    if (rslt != 0) {
        ESP_LOGE(TAG, "AK8963 init failed (rslt=%u); WIA (reg 0x00 at 0x%02X) must return 0x48",
                 (unsigned)rslt, (unsigned)MPU9250_ESP_AK8963_ADDR_7BIT);
        err = mpu9250_esp_err_from_mag_init(rslt);
        goto fail;
    }

    MPU9250_ESP_APPLY(mpu9250_mag_set_mode(&ctx->handle, MPU9250_MAGNETOMETER_MODE_CONTINUOUS2),
                      "mag_set_mode");
    MPU9250_ESP_APPLY(mpu9250_mag_set_bits(&ctx->handle, MPU9250_MAGNETOMETER_BITS_16),
                      "mag_set_bits");

    ctx->initialized = true;
    *out_handle = ctx;

    ESP_LOGI(TAG, "MPU-9250 ready: accel +-2 g, gyro +-2000 dps, mag 16-bit continuous, "
                  "%d Hz sample rate, I2C%d SDA=GPIO%d SCL=GPIO%d %d Hz addr 0x%02X",
             MPU9250_ESP_SAMPLE_RATE_HZ, MPU9250_ESP_I2C_PORT,
             MPU9250_ESP_SDA_GPIO, MPU9250_ESP_SCL_GPIO, MPU9250_ESP_I2C_CLOCK_HZ,
             (unsigned)MPU9250_ESP_MPU_ADDR_7BIT);
    return ESP_OK;

fail:
    mpu9250_esp_release(ctx);
    return err;
}

esp_err_t mpu9250_esp_read(mpu9250_esp_handle_t handle, mpu9250_esp_reading_t *out_reading)
{
    ESP_RETURN_ON_FALSE((handle != NULL) && (out_reading != NULL),
                        ESP_ERR_INVALID_ARG, TAG, "Null argument");

    struct mpu9250_esp_dev_t *ctx = handle;
    ESP_RETURN_ON_FALSE(ctx->initialized, ESP_ERR_INVALID_STATE, TAG, "Not initialized");

    memset(out_reading, 0, sizeof(*out_reading));

    int16_t accel_raw[1][3] = { 0 };
    int16_t gyro_raw[1][3] = { 0 };
    int16_t mag_raw[1][3] = { 0 };
    float accel_g[1][3] = { 0 };
    float gyro_dps[1][3] = { 0 };
    float mag_ut[1][3] = { 0 };
    uint16_t len = 1;

    uint8_t rslt = mpu9250_read(&ctx->handle, accel_raw, accel_g, gyro_raw, gyro_dps,
                                mag_raw, mag_ut, &len);
    if (rslt != 0) {
        ESP_LOGW(TAG, "mpu9250_read failed (rslt=%u)", (unsigned)rslt);
        return mpu9250_esp_err_from_read(rslt);
    }

    for (int i = 0; i < 3; i++) {
        out_reading->accel_g[i] = accel_g[0][i];
        out_reading->gyro_dps[i] = gyro_dps[0][i];
        out_reading->mag_ut[i] = mag_ut[0][i];
    }
    out_reading->timestamp_us = esp_timer_get_time();

    ESP_LOGD(TAG, "a=[%.3f %.3f %.3f] g w=[%.2f %.2f %.2f] dps m=[%.2f %.2f %.2f] uT",
             out_reading->accel_g[0], out_reading->accel_g[1], out_reading->accel_g[2],
             out_reading->gyro_dps[0], out_reading->gyro_dps[1], out_reading->gyro_dps[2],
             out_reading->mag_ut[0], out_reading->mag_ut[1], out_reading->mag_ut[2]);

    return ESP_OK;
}

esp_err_t mpu9250_esp_deinit(mpu9250_esp_handle_t handle)
{
    ESP_RETURN_ON_FALSE(handle != NULL, ESP_ERR_INVALID_ARG, TAG, "Null handle");

    struct mpu9250_esp_dev_t *ctx = handle;
    ESP_RETURN_ON_FALSE(ctx->initialized, ESP_ERR_INVALID_STATE, TAG, "Not initialized");

    ctx->initialized = false;

    if (ctx->handle.mag_inited == 1) {
        uint8_t rslt = mpu9250_mag_deinit(&ctx->handle);
        if (rslt != 0) {
            ESP_LOGW(TAG, "AK8963 power down failed (rslt=%u)", (unsigned)rslt);
        }
    }

    /* Enters sleep mode with the clock stopped and calls the iic_deinit hook, which
     * removes the devices and deletes the bus. */
    uint8_t rslt = mpu9250_deinit(&ctx->handle);
    if (rslt != 0) {
        ESP_LOGW(TAG, "MPU-9250 power down failed (rslt=%u)", (unsigned)rslt);
    }

    mpu9250_esp_release(ctx);
    return ESP_OK;
}
