/*
 * SPDX-FileCopyrightText: 2024 ECE Balloon Bot Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Host (no ESP-IDF, no hardware) regression test for the STM32 UART framing
 * and queue logic. This test exercises the line reassembly logic that lives
 * in the worker task, decoupled from ESP-IDF APIs.
 *
 * Build and run with tests/run_stm32_uart_host_test.sh.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "stm32_uart_protocol.h"

static int failures;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);        \
            failures++;                                                   \
        }                                                                 \
    } while (0)

#define CHECK_STR_EQ(actual, expected)                                    \
    do {                                                                  \
        if (strcmp((actual), (expected)) != 0) {                          \
            printf("FAIL %s:%d: expected \"%s\" got \"%s\"\n",          \
                   __FILE__, __LINE__, expected, actual);                 \
            failures++;                                                   \
        }                                                                 \
    } while (0)

/* ------------------------------------------------------------------------- */
/* Test fixtures                                                             */
/* ------------------------------------------------------------------------- */

static void test_init_deinit(void)
{
    stm32_proto_ctx_t *ctx = stm32_proto_init();
    CHECK(ctx != NULL);
    stm32_proto_deinit(ctx);
}

static void test_empty_input(void)
{
    stm32_proto_ctx_t *ctx = stm32_proto_init();
    char out[129];

    /* No data fed, no lines available */
    CHECK(!stm32_proto_try_receive(ctx, out, sizeof(out)));

    stm32_proto_deinit(ctx);
}

static void test_single_line_lf(void)
{
    stm32_proto_ctx_t *ctx = stm32_proto_init();
    char out[129];

    stm32_proto_feed(ctx, (const uint8_t *)"HELLO\n", 6);
    CHECK(stm32_proto_try_receive(ctx, out, sizeof(out)));
    CHECK_STR_EQ(out, "HELLO");
    CHECK(!stm32_proto_try_receive(ctx, out, sizeof(out)));

    stm32_proto_deinit(ctx);
}

static void test_single_line_crlf(void)
{
    stm32_proto_ctx_t *ctx = stm32_proto_init();
    char out[129];

    stm32_proto_feed(ctx, (const uint8_t *)"HELLO\r\n", 7);
    CHECK(stm32_proto_try_receive(ctx, out, sizeof(out)));
    CHECK_STR_EQ(out, "HELLO");
    CHECK(!stm32_proto_try_receive(ctx, out, sizeof(out)));

    stm32_proto_deinit(ctx);
}

static void test_single_line_cr_only(void)
{
    stm32_proto_ctx_t *ctx = stm32_proto_init();
    char out[129];

    stm32_proto_feed(ctx, (const uint8_t *)"HELLO\r", 6);
    CHECK(stm32_proto_try_receive(ctx, out, sizeof(out)));
    CHECK_STR_EQ(out, "HELLO");
    CHECK(!stm32_proto_try_receive(ctx, out, sizeof(out)));

    stm32_proto_deinit(ctx);
}

static void test_split_line_across_feeds(void)
{
    stm32_proto_ctx_t *ctx = stm32_proto_init();
    char out[129];

    stm32_proto_feed(ctx, (const uint8_t *)"HEL", 3);
    CHECK(!stm32_proto_try_receive(ctx, out, sizeof(out)));

    stm32_proto_feed(ctx, (const uint8_t *)"LO\n", 3);
    CHECK(stm32_proto_try_receive(ctx, out, sizeof(out)));
    CHECK_STR_EQ(out, "HELLO");

    stm32_proto_deinit(ctx);
}

static void test_two_frames_one_feed(void)
{
    stm32_proto_ctx_t *ctx = stm32_proto_init();
    char out[129];

    stm32_proto_feed(ctx, (const uint8_t *)"ONE\nTWO\n", 8);
    CHECK(stm32_proto_try_receive(ctx, out, sizeof(out)));
    CHECK_STR_EQ(out, "ONE");
    CHECK(stm32_proto_try_receive(ctx, out, sizeof(out)));
    CHECK_STR_EQ(out, "TWO");
    CHECK(!stm32_proto_try_receive(ctx, out, sizeof(out)));

    stm32_proto_deinit(ctx);
}

static void test_empty_line_discarded(void)
{
    stm32_proto_ctx_t *ctx = stm32_proto_init();
    char out[129];

    stm32_proto_feed(ctx, (const uint8_t *)"\n", 1);
    CHECK(!stm32_proto_try_receive(ctx, out, sizeof(out)));

    stm32_proto_feed(ctx, (const uint8_t *)"\r\n", 2);
    CHECK(!stm32_proto_try_receive(ctx, out, sizeof(out)));

    stm32_proto_feed(ctx, (const uint8_t *)"\r", 1);
    CHECK(!stm32_proto_try_receive(ctx, out, sizeof(out)));

    stm32_proto_deinit(ctx);
}

static void test_overlong_line_dropped(void)
{
    stm32_proto_ctx_t *ctx = stm32_proto_init();
    char out[129];

    /* 130 bytes payload + \n = overlong (max is 128) */
    char long_line[131];
    memset(long_line, 'A', 130);
    long_line[130] = '\n';

    stm32_proto_feed(ctx, (const uint8_t *)long_line, 131);
    CHECK(!stm32_proto_try_receive(ctx, out, sizeof(out)));

    /* Next valid line should work */
    stm32_proto_feed(ctx, (const uint8_t *)"OK\n", 3);
    CHECK(stm32_proto_try_receive(ctx, out, sizeof(out)));
    CHECK_STR_EQ(out, "OK");

    stm32_proto_deinit(ctx);
}

static void test_max_length_line_accepted(void)
{
    stm32_proto_ctx_t *ctx = stm32_proto_init();
    char out[129];

    /* Exactly 128 bytes payload + \n */
    char max_line[130];
    memset(max_line, 'B', 128);
    max_line[128] = '\n';
    max_line[129] = '\0';

    stm32_proto_feed(ctx, (const uint8_t *)max_line, 129);
    CHECK(stm32_proto_try_receive(ctx, out, sizeof(out)));
    CHECK(strlen(out) == 128);
    for (int i = 0; i < 128; i++) {
        CHECK(out[i] == 'B');
    }

    stm32_proto_deinit(ctx);
}

static void test_invalid_bytes_replaced(void)
{
    stm32_proto_ctx_t *ctx = stm32_proto_init();
    char out[129];

    /* Mix of valid and invalid bytes */
    uint8_t input[] = { 'A', 0x01, 'B', 0x7F, 'C', '\n' };
    stm32_proto_feed(ctx, input, sizeof(input));
    CHECK(stm32_proto_try_receive(ctx, out, sizeof(out)));
    CHECK_STR_EQ(out, "A?B?C");

    stm32_proto_deinit(ctx);
}

static void test_tab_preserved(void)
{
    stm32_proto_ctx_t *ctx = stm32_proto_init();
    char out[129];

    uint8_t input[] = { 'A', '\t', 'B', '\n' };
    stm32_proto_feed(ctx, input, sizeof(input));
    CHECK(stm32_proto_try_receive(ctx, out, sizeof(out)));
    CHECK_STR_EQ(out, "A\tB");

    stm32_proto_deinit(ctx);
}

static void test_recovery_after_overflow(void)
{
    stm32_proto_ctx_t *ctx = stm32_proto_init();
    char out[129];

    /* Feed overlong line (no terminator yet) */
    for (int i = 0; i < 150; i++) {
        stm32_proto_feed(ctx, (const uint8_t *)"X", 1);
    }
    CHECK(!stm32_proto_try_receive(ctx, out, sizeof(out)));

    /* Now send terminator - should drop overflow and reset */
    stm32_proto_feed(ctx, (const uint8_t *)"\n", 1);
    CHECK(!stm32_proto_try_receive(ctx, out, sizeof(out)));

    /* Next valid line should work */
    stm32_proto_feed(ctx, (const uint8_t *)"RECOVERED\n", 10);
    CHECK(stm32_proto_try_receive(ctx, out, sizeof(out)));
    CHECK_STR_EQ(out, "RECOVERED");

    stm32_proto_deinit(ctx);
}

static void test_rx_queue_full_drops_lines(void)
{
    stm32_proto_ctx_t *ctx = stm32_proto_init();
    char out[129];

    /* Fill RX queue (capacity 16) */
    for (int i = 0; i < 16; i++) {
        char line[16];
        snprintf(line, sizeof(line), "LINE%d\n", i);
        stm32_proto_feed(ctx, (const uint8_t *)line, strlen(line));
    }

    /* Queue should be full now; next line should be dropped */
    stm32_proto_feed(ctx, (const uint8_t *)"DROPPED\n", 8);

    /* Drain queue - should get 16 lines */
    int count = 0;
    while (stm32_proto_try_receive(ctx, out, sizeof(out))) {
        count++;
    }
    CHECK(count == 16);

    /* DROPPED line should not appear */
    CHECK(!stm32_proto_try_receive(ctx, out, sizeof(out)));

    stm32_proto_deinit(ctx);
}

static void test_mixed_valid_invalid_sequences(void)
{
    stm32_proto_ctx_t *ctx = stm32_proto_init();
    char out[129];

    /* Valid, overlong, valid, invalid bytes, valid */
    stm32_proto_feed(ctx, (const uint8_t *)"VALID1\n", 7);
    CHECK(stm32_proto_try_receive(ctx, out, sizeof(out)));
    CHECK_STR_EQ(out, "VALID1");

    char long_line[131];
    memset(long_line, 'X', 130);
    long_line[130] = '\n';
    stm32_proto_feed(ctx, (const uint8_t *)long_line, 131);
    CHECK(!stm32_proto_try_receive(ctx, out, sizeof(out)));

    stm32_proto_feed(ctx, (const uint8_t *)"VALID2\n", 7);
    CHECK(stm32_proto_try_receive(ctx, out, sizeof(out)));
    CHECK_STR_EQ(out, "VALID2");

    uint8_t bad[] = { 0x00, 0xFF, 0x80, '\n' };
    stm32_proto_feed(ctx, bad, sizeof(bad));
    CHECK(stm32_proto_try_receive(ctx, out, sizeof(out)));
    CHECK_STR_EQ(out, "???");

    stm32_proto_feed(ctx, (const uint8_t *)"VALID3\n", 7);
    CHECK(stm32_proto_try_receive(ctx, out, sizeof(out)));
    CHECK_STR_EQ(out, "VALID3");

    stm32_proto_deinit(ctx);
}

static void test_partial_crlf_split(void)
{
    stm32_proto_ctx_t *ctx = stm32_proto_init();
    char out[129];

    /* Feed \r in one chunk, \n in next */
    stm32_proto_feed(ctx, (const uint8_t *)"HELLO\r", 6);
    CHECK(stm32_proto_try_receive(ctx, out, sizeof(out)));
    CHECK_STR_EQ(out, "HELLO");

    /* The \n should produce an empty line (discarded) */
    stm32_proto_feed(ctx, (const uint8_t *)"\n", 1);
    CHECK(!stm32_proto_try_receive(ctx, out, sizeof(out)));

    stm32_proto_feed(ctx, (const uint8_t *)"WORLD\n", 6);
    CHECK(stm32_proto_try_receive(ctx, out, sizeof(out)));
    CHECK_STR_EQ(out, "WORLD");

    stm32_proto_deinit(ctx);
}

static void test_null_pointer_safety(void)
{
    stm32_proto_ctx_t *ctx = stm32_proto_init();
    char out[129];

    /* NULL context */
    CHECK(!stm32_proto_try_receive(NULL, out, sizeof(out)));
    stm32_proto_feed(NULL, (const uint8_t *)"TEST\n", 5);

    /* NULL buffer */
    CHECK(!stm32_proto_try_receive(ctx, NULL, sizeof(out)));

    /* Zero capacity */
    CHECK(!stm32_proto_try_receive(ctx, out, 0));

    /* Capacity too small */
    CHECK(!stm32_proto_try_receive(ctx, out, 10));

    stm32_proto_deinit(ctx);

    /* Deinit NULL */
    stm32_proto_deinit(NULL);
}

/* ------------------------------------------------------------------------- */
/* Main                                                                      */
/* ------------------------------------------------------------------------- */

int main(void)
{
    test_init_deinit();
    test_empty_input();
    test_single_line_lf();
    test_single_line_crlf();
    test_single_line_cr_only();
    test_split_line_across_feeds();
    test_two_frames_one_feed();
    test_empty_line_discarded();
    test_overlong_line_dropped();
    test_max_length_line_accepted();
    test_invalid_bytes_replaced();
    test_tab_preserved();
    test_recovery_after_overflow();
    test_rx_queue_full_drops_lines();
    test_mixed_valid_invalid_sequences();
    test_partial_crlf_split();
    test_null_pointer_safety();

    if (failures != 0) {
        printf("%d check(s) failed in STM32 UART protocol host test\n", failures);
        return 1;
    }

    printf("all STM32 UART protocol host checks passed\n");
    return 0;
}