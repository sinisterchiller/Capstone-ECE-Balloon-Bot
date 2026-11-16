/*
 * SPDX-FileCopyrightText: 2024 ECE Balloon Bot Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Host-testable STM32 UART line framing logic implementation.
 *
 * Mirrors the framing logic in drivers/stm32_uart/stm32_uart_esp.c worker task.
 */

#include "stm32_uart_protocol.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Internal context structure */
struct stm32_proto_ctx_t {
    /* RX queue: circular buffer of lines */
    char rx_queue[STM32_PROTO_RX_QUEUE_CAPACITY][STM32_PROTO_MAX_PAYLOAD + 1];
    size_t rx_head;
    size_t rx_tail;
    size_t rx_count;

    /* Line reassembly state */
    char line_buf[STM32_PROTO_MAX_PAYLOAD + 1];
    size_t line_len;
    bool line_overflow;
};

/* Helper: check if character is printable or tab */
static inline bool is_printable_or_tab(unsigned char c)
{
    return (c >= 0x20 && c <= 0x7E) || c == '\t';
}

/* Helper: check if character is a line ending */
static inline bool is_line_ending(unsigned char c)
{
    return c == '\n' || c == '\r';
}

/* Helper: try to enqueue a completed line to RX queue */
static bool rx_queue_push(stm32_proto_ctx_t *ctx, const char *line)
{
    if (ctx->rx_count >= STM32_PROTO_RX_QUEUE_CAPACITY) {
        return false; /* Queue full */
    }
    strncpy(ctx->rx_queue[ctx->rx_head], line, STM32_PROTO_MAX_PAYLOAD);
    ctx->rx_queue[ctx->rx_head][STM32_PROTO_MAX_PAYLOAD] = '\0';
    ctx->rx_head = (ctx->rx_head + 1) % STM32_PROTO_RX_QUEUE_CAPACITY;
    ctx->rx_count++;
    return true;
}

stm32_proto_ctx_t *stm32_proto_init(void)
{
    stm32_proto_ctx_t *ctx = calloc(1, sizeof(*ctx));
    if (!ctx) {
        return NULL;
    }
    ctx->rx_head = 0;
    ctx->rx_tail = 0;
    ctx->rx_count = 0;
    ctx->line_len = 0;
    ctx->line_overflow = false;
    return ctx;
}

void stm32_proto_deinit(stm32_proto_ctx_t *ctx)
{
    if (ctx) {
        free(ctx);
    }
}

void stm32_proto_feed(stm32_proto_ctx_t *ctx, const uint8_t *data, size_t len)
{
    if (!ctx || !data || len == 0) {
        return;
    }

    for (size_t i = 0; i < len; i++) {
        unsigned char c = data[i];

        if (is_line_ending(c)) {
            /* Line terminator found */
            if (!ctx->line_overflow && ctx->line_len > 0) {
                /* Valid complete line - enqueue to RX queue */
                ctx->line_buf[ctx->line_len] = '\0';
                rx_queue_push(ctx, ctx->line_buf);
            } else if (ctx->line_overflow) {
                /* Overlong line dropped, just log in real implementation */
            }
            /* Empty lines (line_len == 0) are silently discarded */

            /* Reset for next line */
            ctx->line_len = 0;
            ctx->line_overflow = false;
        } else {
            /* Data byte */
            if (ctx->line_len >= STM32_PROTO_MAX_PAYLOAD) {
                ctx->line_overflow = true;
                continue;
            }

            /* Replace non-printable with '?' (except tab) */
            if (!is_printable_or_tab(c)) {
                c = '?';
            }
            ctx->line_buf[ctx->line_len++] = (char)c;
        }
    }
}

bool stm32_proto_try_receive(stm32_proto_ctx_t *ctx, char *buffer, size_t capacity)
{
    if (!ctx || !buffer || capacity == 0) {
        return false;
    }
    if (capacity < STM32_PROTO_MAX_PAYLOAD + 1) {
        return false;
    }
    if (ctx->rx_count == 0) {
        return false;
    }

    /* Pop from RX queue */
    strncpy(buffer, ctx->rx_queue[ctx->rx_tail], capacity - 1);
    buffer[capacity - 1] = '\0';
    ctx->rx_tail = (ctx->rx_tail + 1) % STM32_PROTO_RX_QUEUE_CAPACITY;
    ctx->rx_count--;
    return true;
}