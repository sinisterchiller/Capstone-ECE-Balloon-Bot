/**
 * @brief MPU-6500 adapter implementation
 *
 * Forwards to the drivers/mpu6500 component. No I2C, register or sensor logic
 * lives here.
 */

#include "mpu.h"

esp_err_t Mpu::init() {
    if (handle_ != nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    return mpu6500_esp_init(&handle_);
}

esp_err_t Mpu::read(MpuReading *out_reading) {
    if (out_reading == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    if (handle_ == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    return mpu6500_esp_read(handle_, out_reading);
}

esp_err_t Mpu::deinit() {
    if (handle_ == nullptr) {
        return ESP_OK;
    }

    esp_err_t err = mpu6500_esp_deinit(handle_);
    handle_ = nullptr;
    return err;
}
