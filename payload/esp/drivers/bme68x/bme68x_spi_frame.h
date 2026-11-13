/*
 * SPDX-FileCopyrightText: 2024 ECE Balloon Bot Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Construction of the two SPI frames the Bosch BME68x Sensor API needs.
 *
 * The code is free of ESP-IDF dependencies so it can be compiled and tested on
 * the host (see tests/bme68x_host_test.c), while bme68x_esp.c uses the exact
 * same functions on the target.
 */

#pragma once

#include <stdint.h>

#include "bme68x_defs.h"

/*
 * ESP32 CPU-driven SPI is limited to 512 bits (16 FIFO words) per transfer, so
 * a complete frame - control byte plus register bytes - must not exceed 64
 * bytes. The Bosch API never issues more than 51 register bytes (three field
 * blocks); 64 keeps headroom without allowing an unserviceable transfer.
 */
#define BME68X_SPI_FRAME_MAX_BYTES 64
#define BME68X_SPI_REG_MAX_BYTES   (BME68X_SPI_FRAME_MAX_BYTES - 1)

/**
 * @brief Build the read frame for one register block
 *
 * frame[0] is the register address with BME68X_SPI_RD_MSK set; frame[1..len]
 * are the zero bytes clocked out while the sensor drives the data on MISO.
 *
 * @param reg_addr Register address (the read mask may already be set)
 * @param len      Number of register bytes to read, 1..BME68X_SPI_REG_MAX_BYTES
 * @param frame    Output buffer of at least len + 1 bytes
 * @param frame_cap Capacity of frame in bytes
 * @return Total frame length in bytes, or 0 when the request cannot be framed
 */
uint32_t bme68x_spi_read_frame(uint8_t reg_addr, uint32_t len, uint8_t *frame, uint32_t frame_cap);

/**
 * @brief Build the write frame for one register block
 *
 * frame[0] is the register address with BME68X_SPI_WR_MSK applied; frame[1..len]
 * are copied unchanged from reg_data. The Bosch layer interleaves further
 * address/data pairs into reg_data for multi-register writes
 * (bme68x_set_regs()), so this single frame carries all of them and CS stays
 * low between an address and its data.
 *
 * @param reg_addr Register address
 * @param reg_data Register/data bytes as produced by the Bosch layer
 * @param len      Number of bytes in reg_data, 1..BME68X_SPI_REG_MAX_BYTES
 * @param frame    Output buffer of at least len + 1 bytes
 * @param frame_cap Capacity of frame in bytes
 * @return Total frame length in bytes, or 0 when the request cannot be framed
 */
uint32_t bme68x_spi_write_frame(uint8_t reg_addr,
                                const uint8_t *reg_data,
                                uint32_t len,
                                uint8_t *frame,
                                uint32_t frame_cap);
