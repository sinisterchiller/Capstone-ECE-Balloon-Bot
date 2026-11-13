/*
 * SPDX-FileCopyrightText: 2024 ECE Balloon Bot Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ESP-IDF SPI adapter for the Bosch BME68x Sensor API.
 *
 * Vendor source: boschsensortec/BME68x_SensorAPI v4.4.8,
 * commit 80ea120a8b8ac987d7d79eb68a9ed796736be845 (see README.md).
 * Vendor files (bme68x.c, bme68x.h, bme68x_defs.h, LICENSE) are unmodified.
 */

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "driver/spi_master.h"

#include "bme68x.h"
#include "bme68x_esp.h"
#include "bme68x_spi_frame.h"

static const char *TAG = "bme68x_esp";

/* SPI framing: 1 MHz, mode 0, one device per bus (CS handled by the SPI driver) */
#define BME68X_SPI_CLOCK_HZ       1000000

/* Ambient temperature used by the Bosch API to compute the heater resistance */
#define BME68X_DEFAULT_AMB_TEMP_C 25

/* Datasheet limits for the heater profile (BME68X_FORCED_MODE) */
#define BME68X_HEATR_TEMP_MIN_C   200
#define BME68X_HEATR_TEMP_MAX_C   400
#define BME68X_HEATR_DUR_MIN_MS   1
#define BME68X_HEATR_DUR_MAX_MS   4032 /* largest gas-wait value encodable by the API */

/* Poll period while waiting for a forced-mode conversion to finish */
#define BME68X_POLL_INTERVAL_MS   5

/* Slack on top of conversion + heater time, absorbs SPI/polling latency */
#define BME68X_MEAS_MARGIN_US     25000

struct bme68x_esp_dev_t {
    struct bme68x_dev dev;
    spi_device_handle_t spi_handle;
    spi_host_device_t spi_host;
    bool bus_initialized;
    bool initialized;
    uint32_t meas_duration_us; /* TPH conversion + heater time, computed at init */
};

/*
 * Bosch SPI callback contract (see bme68x_get_regs()/bme68x_set_regs() in bme68x.c):
 *
 *  - read():  reg_addr already carries BME68X_SPI_RD_MSK (0x80). The sensor starts
 *             driving the requested bytes on the clocks that follow the address
 *             byte, so a single full-duplex transfer of len + 1 bytes is used and
 *             rx[1..len] holds the register data.
 *  - write(): reg_addr is already masked with BME68X_SPI_WR_MSK (0x7f) and reg_data
 *             holds the register/data bytes already interleaved by the vendor layer.
 *             The complete buffer must be sent inside one CS-low frame:
 *             tx = { reg_addr, reg_data[0..len-1] }. Splitting it per byte would
 *             raise CS between a register address and its data.
 *
 * Neither callback checks the "initialized" flag: bme68x_init() issues reads and
 * writes before init has completed, so the SPI handle is the only precondition.
 */

static int8_t bme68x_spi_read(uint8_t reg_addr, uint8_t *reg_data, uint32_t len, void *intf_ptr)
{
    struct bme68x_esp_dev_t *ctx = (struct bme68x_esp_dev_t *)intf_ptr;

    if ((ctx == NULL) || (ctx->spi_handle == NULL)) {
        return BME68X_E_NULL_PTR;
    }
    if (len == 0) {
        return BME68X_OK;
    }
    if (reg_data == NULL) {
        return BME68X_E_NULL_PTR;
    }

    uint8_t tx_buf[BME68X_SPI_FRAME_MAX_BYTES] = { 0 };
    uint8_t rx_buf[BME68X_SPI_FRAME_MAX_BYTES] = { 0 };

    uint32_t frame_len = bme68x_spi_read_frame(reg_addr, len, tx_buf, sizeof(tx_buf));
    if (frame_len == 0) {
        ESP_LOGE(TAG, "SPI read of %" PRIu32 " bytes cannot be framed (max %d)",
                 len, BME68X_SPI_REG_MAX_BYTES);
        return BME68X_E_INVALID_LENGTH;
    }

    spi_transaction_t trans = {
        .length = frame_len * 8,
        .rxlength = frame_len * 8,
        .tx_buffer = tx_buf,
        .rx_buffer = rx_buf,
    };

    esp_err_t err = spi_device_polling_transmit(ctx->spi_handle, &trans);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "SPI read of reg 0x%02X failed: %s", reg_addr, esp_err_to_name(err));
        return BME68X_E_COM_FAIL;
    }

    memcpy(reg_data, &rx_buf[1], len);
    return BME68X_OK;
}

static int8_t bme68x_spi_write(uint8_t reg_addr, const uint8_t *reg_data, uint32_t len, void *intf_ptr)
{
    struct bme68x_esp_dev_t *ctx = (struct bme68x_esp_dev_t *)intf_ptr;

    if ((ctx == NULL) || (ctx->spi_handle == NULL)) {
        return BME68X_E_NULL_PTR;
    }
    if (len == 0) {
        return BME68X_OK;
    }
    if (reg_data == NULL) {
        return BME68X_E_NULL_PTR;
    }

    uint8_t tx_buf[BME68X_SPI_FRAME_MAX_BYTES];

    uint32_t frame_len = bme68x_spi_write_frame(reg_addr, reg_data, len, tx_buf, sizeof(tx_buf));
    if (frame_len == 0) {
        ESP_LOGE(TAG, "SPI write of %" PRIu32 " bytes cannot be framed (max %d)",
                 len, BME68X_SPI_REG_MAX_BYTES);
        return BME68X_E_INVALID_LENGTH;
    }

    spi_transaction_t trans = {
        .length = frame_len * 8,
        .tx_buffer = tx_buf,
    };

    esp_err_t err = spi_device_polling_transmit(ctx->spi_handle, &trans);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "SPI write of reg 0x%02X failed: %s", reg_addr, esp_err_to_name(err));
        return BME68X_E_COM_FAIL;
    }

    return BME68X_OK;
}

/* Delay callback for the Bosch API; the sensor is always driven from task context */
static void bme68x_delay_us(uint32_t period_us, void *intf_ptr)
{
    (void)intf_ptr;

    if (period_us >= 1000) {
        TickType_t ticks = pdMS_TO_TICKS((period_us + 999) / 1000);
        vTaskDelay(ticks > 0 ? ticks : 1);
    } else {
        esp_rom_delay_us(period_us);
    }
}

/* Ticks that delay at least duration_us (rounded up, never down) */
static TickType_t bme68x_ticks_for_us(uint32_t duration_us)
{
    const uint32_t tick_us = (uint32_t)portTICK_PERIOD_MS * 1000U;

    return (TickType_t)((duration_us + tick_us - 1U) / tick_us);
}

static esp_err_t bme68x_esp_err_from_rslt(int8_t rslt)
{
    switch (rslt) {
        case BME68X_OK:
            return ESP_OK;
        case BME68X_E_NULL_PTR:
            return ESP_ERR_INVALID_ARG;
        case BME68X_E_COM_FAIL:
            return ESP_ERR_INVALID_RESPONSE;
        case BME68X_E_DEV_NOT_FOUND:
            return ESP_ERR_NOT_FOUND;
        case BME68X_E_INVALID_LENGTH:
            return ESP_ERR_INVALID_SIZE;
        case BME68X_W_NO_NEW_DATA:
            return ESP_ERR_NOT_FINISHED;
        default:
            return ESP_FAIL;
    }
}

/* Release the SPI device, the bus and the context (partial-init safe) */
static void bme68x_esp_release(struct bme68x_esp_dev_t *ctx)
{
    if (ctx->spi_handle != NULL) {
        spi_bus_remove_device(ctx->spi_handle);
        ctx->spi_handle = NULL;
    }
    if (ctx->bus_initialized) {
        spi_bus_free(ctx->spi_host);
        ctx->bus_initialized = false;
    }
    free(ctx);
}

esp_err_t bme68x_esp_init(const bme68x_spi_pins_t *pins,
                          const bme68x_sensor_config_t *config,
                          bme68x_esp_handle_t *out_handle)
{
    ESP_RETURN_ON_FALSE(out_handle != NULL, ESP_ERR_INVALID_ARG, TAG, "Null handle");
    *out_handle = NULL;

    ESP_RETURN_ON_FALSE((pins != NULL) && (config != NULL), ESP_ERR_INVALID_ARG, TAG, "Null argument");
    ESP_RETURN_ON_FALSE((pins->mosi_gpio >= 0) && (pins->miso_gpio >= 0) &&
                            (pins->sclk_gpio >= 0) && (pins->cs_gpio >= 0),
                        ESP_ERR_INVALID_ARG, TAG, "Negative GPIO in pin configuration");
    ESP_RETURN_ON_FALSE((config->heatr_temp >= BME68X_HEATR_TEMP_MIN_C) &&
                            (config->heatr_temp <= BME68X_HEATR_TEMP_MAX_C),
                        ESP_ERR_INVALID_ARG, TAG, "Heater temperature %u out of range",
                        (unsigned)config->heatr_temp);
    ESP_RETURN_ON_FALSE((config->heatr_dur >= BME68X_HEATR_DUR_MIN_MS) &&
                            (config->heatr_dur <= BME68X_HEATR_DUR_MAX_MS),
                        ESP_ERR_INVALID_ARG, TAG, "Heater duration %u out of range",
                        (unsigned)config->heatr_dur);

    struct bme68x_esp_dev_t *ctx = calloc(1, sizeof(*ctx));
    ESP_RETURN_ON_FALSE(ctx != NULL, ESP_ERR_NO_MEM, TAG, "Failed to allocate device context");

    ctx->spi_host = SPI2_HOST;

    const spi_bus_config_t bus_cfg = {
        .mosi_io_num = pins->mosi_gpio,
        .miso_io_num = pins->miso_gpio,
        .sclk_io_num = pins->sclk_gpio,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = BME68X_SPI_FRAME_MAX_BYTES,
    };

    /* Register transfers are at most a few dozen bytes, so CPU-driven transfers
     * are used. This keeps the callback buffers valid regardless of where the
     * calling task's stack lives (DMA needs internal RAM). */
    esp_err_t err = spi_bus_initialize(ctx->spi_host, &bus_cfg, SPI_DMA_DISABLED);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPI bus init failed: %s", esp_err_to_name(err));
        free(ctx);
        return err;
    }
    ctx->bus_initialized = true;

    const spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = BME68X_SPI_CLOCK_HZ,
        .mode = 0, /* CPOL=0, CPHA=0 */
        .spics_io_num = pins->cs_gpio,
        .queue_size = 1,
        .flags = 0, /* full duplex: the sensor has separate SDI and SDO lines */
    };

    err = spi_bus_add_device(ctx->spi_host, &dev_cfg, &ctx->spi_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPI device add failed: %s", esp_err_to_name(err));
        bme68x_esp_release(ctx);
        return err;
    }

    ctx->dev.intf = BME68X_SPI_INTF;
    ctx->dev.read = bme68x_spi_read;
    ctx->dev.write = bme68x_spi_write;
    ctx->dev.delay_us = bme68x_delay_us;
    ctx->dev.intf_ptr = ctx;
    ctx->dev.amb_temp = BME68X_DEFAULT_AMB_TEMP_C;

    int8_t rslt = bme68x_init(&ctx->dev);
    if (rslt != BME68X_OK) {
        ESP_LOGE(TAG, "bme68x_init failed: %s", bme68x_esp_strerror(rslt));
        bme68x_esp_release(ctx);
        return bme68x_esp_err_from_rslt(rslt);
    }

    struct bme68x_conf conf = {
        .os_temp = config->os_temp,
        .os_pres = config->os_pres,
        .os_hum = config->os_hum,
        .filter = config->filter,
        .odr = config->odr,
    };

    rslt = bme68x_set_conf(&conf, &ctx->dev);
    if (rslt != BME68X_OK) {
        ESP_LOGE(TAG, "bme68x_set_conf failed: %s", bme68x_esp_strerror(rslt));
        bme68x_esp_release(ctx);
        return bme68x_esp_err_from_rslt(rslt);
    }
    if (ctx->dev.info_msg != BME68X_OK) {
        ESP_LOGW(TAG, "Oversampling/filter/ODR value clamped by the Bosch API (info_msg=%d)",
                 ctx->dev.info_msg);
    }

    struct bme68x_heatr_conf heatr_conf = {
        .enable = BME68X_ENABLE,
        .heatr_temp = config->heatr_temp,
        .heatr_dur = config->heatr_dur,
    };

    rslt = bme68x_set_heatr_conf(BME68X_FORCED_MODE, &heatr_conf, &ctx->dev);
    if (rslt != BME68X_OK) {
        ESP_LOGE(TAG, "bme68x_set_heatr_conf failed: %s", bme68x_esp_strerror(rslt));
        bme68x_esp_release(ctx);
        return bme68x_esp_err_from_rslt(rslt);
    }

    /* The Bosch layer clamps out-of-range oversampling values to the register
     * field width, so read back what the sensor will actually use before timing
     * the conversion. bme68x_get_meas_dur() covers the TPH conversion only; the
     * heater runs for heatr_dur on top of it. The requested heater duration is an
     * upper bound because the vendor quantizes it down into gas_wait steps. */
    struct bme68x_conf applied_conf = { 0 };
    rslt = bme68x_get_conf(&applied_conf, &ctx->dev);
    if (rslt != BME68X_OK) {
        ESP_LOGE(TAG, "bme68x_get_conf failed: %s", bme68x_esp_strerror(rslt));
        bme68x_esp_release(ctx);
        return bme68x_esp_err_from_rslt(rslt);
    }

    uint32_t tph_dur_us = bme68x_get_meas_dur(BME68X_FORCED_MODE, &applied_conf, &ctx->dev);
    ctx->meas_duration_us = tph_dur_us + ((uint32_t)config->heatr_dur * 1000U);

    ctx->initialized = true;
    *out_handle = ctx;

    ESP_LOGI(TAG, "BME68x ready: chip_id=0x%02X variant_id=%" PRIu32
                  " tph_dur=%" PRIu32 "us heatr=%ums meas_duration=%" PRIu32 "us",
             ctx->dev.chip_id, ctx->dev.variant_id, tph_dur_us, (unsigned)config->heatr_dur,
             ctx->meas_duration_us);
    return ESP_OK;
}

esp_err_t bme68x_esp_deinit(bme68x_esp_handle_t handle)
{
    ESP_RETURN_ON_FALSE(handle != NULL, ESP_ERR_INVALID_ARG, TAG, "Null handle");

    struct bme68x_esp_dev_t *ctx = handle;

    ctx->initialized = false;

    /* Leave the sensor in sleep mode; the callbacks stay usable until the SPI
     * device is removed below. */
    int8_t rslt = bme68x_soft_reset(&ctx->dev);
    if (rslt != BME68X_OK) {
        ESP_LOGW(TAG, "Soft reset during deinit failed: %s", bme68x_esp_strerror(rslt));
    }

    bme68x_esp_release(ctx);
    return ESP_OK;
}

esp_err_t bme68x_esp_measure(bme68x_esp_handle_t handle, bme68x_measurement_t *out_data)
{
    ESP_RETURN_ON_FALSE((handle != NULL) && (out_data != NULL),
                        ESP_ERR_INVALID_ARG, TAG, "Null argument");

    struct bme68x_esp_dev_t *ctx = handle;
    ESP_RETURN_ON_FALSE(ctx->initialized, ESP_ERR_INVALID_STATE, TAG, "Not initialized");

    memset(out_data, 0, sizeof(*out_data));

    int8_t rslt = bme68x_set_op_mode(BME68X_FORCED_MODE, &ctx->dev);
    if (rslt != BME68X_OK) {
        ESP_LOGW(TAG, "Setting forced mode failed: %s", bme68x_esp_strerror(rslt));
        return bme68x_esp_err_from_rslt(rslt);
    }

    /* Datasheet 5.3.5 ("in forced mode only field 0 is used") and 5.3.6.1 (new_data
     * marks data "not already read-out"): field 0 is overwritten by the running
     * conversion and new_data only clears when the field is read. A conversion that
     * completed after the previous call had already returned (for example
     * following a timeout) therefore leaves new_data set, and a bme68x_get_data()
     * issued right after the trigger would return that earlier measurement while
     * the conversion this call started stays unread - a permanent one-step lag.
     * Waiting out the conversion and heater time before the first read, as
     * examples/forced_mode does, guarantees field 0 holds this call's result. */
    vTaskDelay(bme68x_ticks_for_us(ctx->meas_duration_us));

    /* The conversion window is over; this margin only absorbs a sensor that does
     * not raise new_data exactly at the computed duration. */
    const int64_t deadline_us = esp_timer_get_time() + BME68X_MEAS_MARGIN_US;
    struct bme68x_data data = { 0 };
    uint8_t n_data = 0;

    for (;;) {
        rslt = bme68x_get_data(BME68X_FORCED_MODE, &data, &n_data, &ctx->dev);
        if ((rslt == BME68X_OK) && (n_data > 0)) {
            break;
        }
        if (rslt < 0) {
            ESP_LOGW(TAG, "bme68x_get_data failed: %s", bme68x_esp_strerror(rslt));
            return bme68x_esp_err_from_rslt(rslt);
        }
        if ((rslt != BME68X_OK) && (rslt != BME68X_W_NO_NEW_DATA)) {
            /* Only "no new data yet" is expected while polling a conversion. */
            ESP_LOGW(TAG, "Unexpected bme68x_get_data status: %s", bme68x_esp_strerror(rslt));
            return ESP_FAIL;
        }
        if (esp_timer_get_time() >= deadline_us) {
            ESP_LOGW(TAG, "No new data in the %d us margin after a %" PRIu32 " us conversion",
                     (int)BME68X_MEAS_MARGIN_US, ctx->meas_duration_us);
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay(pdMS_TO_TICKS(BME68X_POLL_INTERVAL_MS));
    }

    /* Bosch returns compensated values already. bme68x_defs.h enables
     * BME68X_USE_FPU unless the build defines BME68X_DO_NOT_USE_FPU, so the
     * default is temperature in degC, pressure in Pa and humidity in %RH. The
     * vendor header comment still claims %RH * 1000 for the float humidity
     * field, which is stale. With BME68X_DO_NOT_USE_FPU the API reports
     * degC * 100 and %RH * 1000 instead, which is scaled here. */
#ifdef BME68X_USE_FPU
    const float temperature = data.temperature;
    const float pressure = data.pressure;
    const float humidity = data.humidity;
    const float gas_resistance = data.gas_resistance;
#else
    const float temperature = (float)data.temperature / 100.0f;
    const float pressure = (float)data.pressure;
    const float humidity = (float)data.humidity / 1000.0f;
    const float gas_resistance = (float)data.gas_resistance;
#endif

    out_data->temperature = temperature;
    out_data->pressure = pressure;
    out_data->humidity = humidity;

    out_data->gas_index = data.gas_index;
    out_data->meas_index = data.meas_index;
    out_data->gas_valid = (data.status & BME68X_GASM_VALID_MSK) != 0;
    out_data->heat_stable = (data.status & BME68X_HEAT_STAB_MSK) != 0;

    /* Reject invalid gas data explicitly instead of forwarding a meaningless
     * resistance; T/RH/P from the same conversion stay usable. */
    out_data->gas_resistance = out_data->gas_valid ? gas_resistance : 0.0f;
    if (!out_data->gas_valid) {
        ESP_LOGD(TAG, "Gas measurement flagged invalid by the sensor, resistance discarded");
    }

    out_data->timestamp_us = esp_timer_get_time();

    ESP_LOGD(TAG, "T=%.2f C P=%.0f Pa RH=%.2f %% gas=%.0f Ohm valid=%d stable=%d",
             out_data->temperature, out_data->pressure, out_data->humidity,
             out_data->gas_resistance, out_data->gas_valid, out_data->heat_stable);

    return ESP_OK;
}

struct bme68x_dev *bme68x_esp_get_dev(bme68x_esp_handle_t handle)
{
    if (handle == NULL) {
        return NULL;
    }

    struct bme68x_esp_dev_t *ctx = handle;
    return &ctx->dev;
}

const char *bme68x_esp_strerror(int8_t rslt)
{
    switch (rslt) {
        case BME68X_OK: return "Success";
        case BME68X_E_NULL_PTR: return "Null pointer";
        case BME68X_E_COM_FAIL: return "Communication failure";
        case BME68X_E_DEV_NOT_FOUND: return "Device not found";
        case BME68X_E_INVALID_LENGTH: return "Invalid length";
        case BME68X_E_SELF_TEST: return "Self test failed";
        case BME68X_W_DEFINE_OP_MODE: return "Define operation mode";
        case BME68X_W_NO_NEW_DATA: return "No new data";
        case BME68X_W_DEFINE_SHD_HEATR_DUR: return "Define shared heater duration";
        default: return "Unknown error";
    }
}
