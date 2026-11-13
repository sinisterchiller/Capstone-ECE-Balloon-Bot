/*
 * SPDX-FileCopyrightText: 2024 ECE Balloon Bot Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Host (no ESP-IDF, no hardware) regression test for the C++ Mpu wrapper in
 * main/mpu.cpp.
 *
 * main/mpu.cpp only forwards to the drivers/mpu9250 adapter, so the test links
 * it against a fake adapter (mpu9250_esp_init/read/deinit) plus the host
 * esp_err.h stand-in in tests/stubs/. That covers the wrapper's own contract,
 * which the driver-level test cannot reach:
 *
 *  - read() before init() and init() twice are rejected without touching the
 *    adapter (ESP_ERR_INVALID_STATE), read(NULL) is ESP_ERR_INVALID_ARG,
 *  - a successful init() makes is_initialized() true and read() forwards the
 *    adapter's error code and its sample unchanged,
 *  - deinit() is idempotent, releases the handle, and the destructor powers the
 *    sensor down for a wrapper that is destroyed while initialized,
 *  - a failed init() leaves the wrapper uninitialized, so deinit() afterwards is
 *    a no-op instead of a double release.
 *
 * Build and run with tests/run_mpu9250_host_test.sh.
 */

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "mpu.h"

static int failures;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
            failures++;                                                    \
        }                                                                  \
    } while (0)

/* ------------------------------------------------------------------------- */
/* Fake drivers/mpu9250 adapter behind the mpu9250_esp_* seam                 */
/* ------------------------------------------------------------------------- */

struct fake_adapter {
    unsigned init_calls;
    unsigned read_calls;
    unsigned deinit_calls;
    unsigned last_read_handle_valid;
    esp_err_t init_result;
    esp_err_t read_result;
    esp_err_t deinit_result;
    mpu9250_esp_reading_t next_reading;
};

static fake_adapter g_fake;

/* mpu9250_esp_dev_t is opaque, so the fake hands out a non-NULL token address. */
static mpu9250_esp_handle_t fake_handle(void)
{
    return reinterpret_cast<mpu9250_esp_handle_t>(static_cast<uintptr_t>(0x1));
}

static void fake_reset()
{
    std::memset(&g_fake, 0, sizeof(g_fake));
    g_fake.init_result = ESP_OK;
    g_fake.read_result = ESP_OK;
    g_fake.deinit_result = ESP_OK;
}

extern "C" esp_err_t mpu9250_esp_init(mpu9250_esp_handle_t *out_handle)
{
    g_fake.init_calls++;

    /* The real adapter sets *out_handle = NULL before it touches the bus and
     * leaves it NULL when initialization fails. */
    *out_handle = (g_fake.init_result == ESP_OK) ? fake_handle() : nullptr;

    return g_fake.init_result;
}

extern "C" esp_err_t mpu9250_esp_read(mpu9250_esp_handle_t handle,
                                      mpu9250_esp_reading_t *out_reading)
{
    g_fake.read_calls++;
    g_fake.last_read_handle_valid = (handle == fake_handle()) ? 1U : 0U;

    if (g_fake.read_result != ESP_OK) {
        return g_fake.read_result;
    }

    *out_reading = g_fake.next_reading;
    return ESP_OK;
}

extern "C" esp_err_t mpu9250_esp_deinit(mpu9250_esp_handle_t handle)
{
    g_fake.deinit_calls++;
    g_fake.last_read_handle_valid = (handle == fake_handle()) ? 1U : 0U;

    return g_fake.deinit_result;
}

/* ------------------------------------------------------------------------- */
/* Tests                                                                     */
/* ------------------------------------------------------------------------- */

static void test_read_before_init_is_rejected(void)
{
    fake_reset();

    Mpu mpu;
    static const mpu9250_esp_reading_t zero_reading = {};
    mpu9250_esp_reading_t reading = zero_reading;

    CHECK(!mpu.is_initialized());
    CHECK(mpu.read(&reading) == ESP_ERR_INVALID_STATE);
    CHECK(g_fake.read_calls == 0); /* rejected by the wrapper, not the adapter */

    /* NULL is rejected before the initialization check. */
    CHECK(mpu.read(nullptr) == ESP_ERR_INVALID_ARG);
    CHECK(g_fake.read_calls == 0);
}

static void test_init_twice_is_rejected(void)
{
    fake_reset();

    Mpu mpu;
    CHECK(mpu.init() == ESP_OK);
    CHECK(mpu.is_initialized());
    CHECK(g_fake.init_calls == 1);

    CHECK(mpu.init() == ESP_ERR_INVALID_STATE);
    CHECK(g_fake.init_calls == 1); /* the adapter was not asked a second time */
    CHECK(mpu.is_initialized());

    CHECK(mpu.deinit() == ESP_OK);
}

static void test_read_forwards_sample_and_errors(void)
{
    fake_reset();

    Mpu mpu;
    CHECK(mpu.init() == ESP_OK);

    /* The nine axes and the timestamp reach the caller unchanged. */
    g_fake.next_reading.accel_g[0] = 1.0f;
    g_fake.next_reading.accel_g[1] = -0.5f;
    g_fake.next_reading.accel_g[2] = 0.25f;
    g_fake.next_reading.gyro_dps[0] = 10.0f;
    g_fake.next_reading.gyro_dps[1] = -20.0f;
    g_fake.next_reading.gyro_dps[2] = 2.5f;
    g_fake.next_reading.mag_ut[0] = 71.25f;
    g_fake.next_reading.mag_ut[1] = -30.0f;
    g_fake.next_reading.mag_ut[2] = 9.0f;
    g_fake.next_reading.timestamp_us = 123456789;

    mpu9250_esp_reading_t reading = {};
    CHECK(mpu.read(&reading) == ESP_OK);
    CHECK(g_fake.read_calls == 1);
    CHECK(g_fake.last_read_handle_valid == 1); /* the handle from init() is reused */
    CHECK(reading.accel_g[0] == 1.0f);
    CHECK(reading.accel_g[1] == -0.5f);
    CHECK(reading.gyro_dps[1] == -20.0f);
    CHECK(reading.mag_ut[0] == 71.25f);
    CHECK(reading.timestamp_us == 123456789);

    /* Adapter errors are returned unchanged (ESP_ERR_INVALID_RESPONSE is what a
     * failed I2C register read produces). */
    g_fake.read_result = ESP_ERR_INVALID_RESPONSE;
    CHECK(mpu.read(&reading) == ESP_ERR_INVALID_RESPONSE);
    CHECK(g_fake.read_calls == 2);

    CHECK(mpu.deinit() == ESP_OK);
}

static void test_deinit_is_idempotent(void)
{
    fake_reset();

    Mpu mpu;
    CHECK(mpu.init() == ESP_OK);

    CHECK(mpu.deinit() == ESP_OK);
    CHECK(g_fake.deinit_calls == 1);
    CHECK(!mpu.is_initialized());

    /* Already released: no second adapter call. */
    CHECK(mpu.deinit() == ESP_OK);
    CHECK(g_fake.deinit_calls == 1);

    /* ... and the wrapper is reusable afterwards. */
    CHECK(mpu.init() == ESP_OK);
    CHECK(mpu.is_initialized());
    CHECK(mpu.deinit() == ESP_OK);
    CHECK(g_fake.deinit_calls == 2);
}

static void test_destructor_powers_down(void)
{
    fake_reset();

    {
        Mpu mpu;
        CHECK(mpu.init() == ESP_OK);
    }

    CHECK(g_fake.deinit_calls == 1);
    CHECK(g_fake.last_read_handle_valid == 1);
}

static void test_failed_init_leaves_wrapper_uninitialized(void)
{
    fake_reset();
    g_fake.init_result = ESP_ERR_NOT_FOUND; /* no MPU-9250 answered on the bus */

    Mpu mpu;
    CHECK(mpu.init() == ESP_ERR_NOT_FOUND);
    CHECK(!mpu.is_initialized());

    mpu9250_esp_reading_t reading = {};
    CHECK(mpu.read(&reading) == ESP_ERR_INVALID_STATE);
    CHECK(g_fake.read_calls == 0);

    /* The real adapter has already released the bus, so the wrapper must not
     * call mpu9250_esp_deinit() with a NULL handle. */
    CHECK(mpu.deinit() == ESP_OK);
    CHECK(g_fake.deinit_calls == 0);
}

int main()
{
    test_read_before_init_is_rejected();
    test_init_twice_is_rejected();
    test_read_forwards_sample_and_errors();
    test_deinit_is_idempotent();
    test_destructor_powers_down();
    test_failed_init_leaves_wrapper_uninitialized();

    if (failures != 0) {
        std::printf("%d check(s) failed in the Mpu wrapper host test\n", failures);
        return 1;
    }

    std::printf("all Mpu wrapper host checks passed\n");
    return 0;
}
