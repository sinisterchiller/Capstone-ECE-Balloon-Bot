/*
 * SPDX-FileCopyrightText: 2024 ECE Balloon Bot Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Host-testable STM32 UART line framing logic.
 *
 * This is a standalone implementation of the newline-delimited framing used
 * by the ESP-IDF worker task, with no ESP-IDF dependencies. It is used by
 * tests/stm32_uart_protocol_host_test.c to verify framing edge cases.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define STM32_PROTO_MAX_PAYLOAD 128
#define STM32_PROTO_RX_QUEUE_CAPACITY 16

typedef struct stm32_proto_ctx_t stm32_proto_ctx_t;

/**
 * @brief Initialize a new protocol context
 * @return Opaque context pointer, or NULL on allocation failure
 */
stm32_proto_ctx_t *stm32_proto_init(void);

/**
 * @brief Deinitialize and free a protocol context
 * @param ctx Context from stm32_proto_init(); NULL is safe
 */
void stm32_proto_deinit(stm32_proto_ctx_t *ctx);

/**
 * @brief Feed raw bytes into the framing reassembler
 * @param ctx Context from stm32_proto_init()
 * @param data Input bytes
 * @param len Number of bytes in data
 */
void stm32_proto_feed(stm32_proto_ctx_t *ctx, const uint8_t *data, size_t len);

/**
 * @brief Try to receive a complete line from the RX queue (non-blocking)
 * @param ctx Context from stm32_proto_init()
 * @param buffer Output buffer for the line (without terminator)
 * @param capacity Size of buffer (must be >= STM32_PROTO_MAX_PAYLOAD + 1)
 * @return true if a line was copied, false if queue empty or on error
 */
bool stm32_proto_try_receive(stm32_proto_ctx_t *ctx, char *buffer, size_t capacity);