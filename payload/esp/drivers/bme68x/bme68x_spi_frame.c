/*
 * SPDX-FileCopyrightText: 2024 ECE Balloon Bot Team
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdbool.h>
#include <string.h>

#include "bme68x_spi_frame.h"

static bool frame_request_valid(uint32_t len, uint32_t frame_cap)
{
    return (len > 0) && (len <= BME68X_SPI_REG_MAX_BYTES) && (frame_cap >= (len + 1U));
}

uint32_t bme68x_spi_read_frame(uint8_t reg_addr, uint32_t len, uint8_t *frame, uint32_t frame_cap)
{
    if ((frame == NULL) || !frame_request_valid(len, frame_cap)) {
        return 0;
    }

    /* Idempotent: bme68x_get_regs() already sets the bit for SPI. */
    frame[0] = (uint8_t)(reg_addr | BME68X_SPI_RD_MSK);
    memset(&frame[1], 0, len);

    return len + 1U;
}

uint32_t bme68x_spi_write_frame(uint8_t reg_addr,
                                const uint8_t *reg_data,
                                uint32_t len,
                                uint8_t *frame,
                                uint32_t frame_cap)
{
    if ((reg_data == NULL) || (frame == NULL) || !frame_request_valid(len, frame_cap)) {
        return 0;
    }

    frame[0] = (uint8_t)(reg_addr & BME68X_SPI_WR_MSK);
    memcpy(&frame[1], reg_data, len);

    return len + 1U;
}
