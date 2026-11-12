/*
 * Host tests for the MPU orientation snapshot consumer
 * (CM0PLUS/Core/Src/mpu_status.c).
 *
 * The consumer is pure C: the caller supplies the current value and the rate of
 * its own clock, and the shared status is an ordinary object here instead of a
 * fixed SRAM1 address, so the whole read path (seqlock retry, initialisation
 * marker, first-observation gate, freshness on the consumer's clock) runs
 * unchanged on the development machine. GNSS_VCP_BARRIER_HOST turns the target
 * memory barrier of gnss_vcp_mailbox.h into a no-op, which is all a
 * single-threaded host can honour.
 *
 * The clock scenarios below are the point of these tests: CPU1 (the producer)
 * stamps readings with its SysTick milliseconds starting at its own reset, CPU2
 * (the consumer) counts RTC ticks at 1024 Hz from the free-running RTC
 * counter. Origins and rates therefore have no relation, and no arithmetic may
 * mix them. The payload expires orientation data after 500 ms on CPU2's clock.
 *
 * Build and run from the MXproject directory:
 *
 *   cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
 *      -DGNSS_VCP_BARRIER_HOST \
 *      -I Common/GnssVcp -I CM0PLUS/Core/Inc \
 *      CM0PLUS/Core/Src/mpu_status.c \
 *      Common/Telemetry/telemetry_mpu_status_test.c \
 *      -o /tmp/telemetry_mpu_status_test && /tmp/telemetry_mpu_status_test
 */

#include "mpu_status.h"

#include <stdio.h>
#include <string.h>

static unsigned int failures;

/* CPU1 SysTick milliseconds when the reference reading was taken. */
static const uint32_t PRODUCER_STAMP_MS = 1000u;
static const int16_t REF_HEADING = 2008;   /* 125.50 * 16 */
static const int16_t REF_ROLL = -68;       /* -4.25 * 16 */
static const int16_t REF_PITCH = 204;      /* 12.75 * 16 */

/* CPU2 counts RTC ticks: 1024 per second in binary mode. */
static const uint32_t CONSUMER_TICKS_PER_SECOND = 1024u;
/* A consumer counter origin that is unrelated to CPU1's. */
static const uint32_t CONSUMER_ORIGIN_TICKS = 0x40000000u;
/* The payload's freshness observation interval (~100 ms at 1024 Hz). */
static const uint32_t FRESHNESS_INTERVAL_TICKS = CONSUMER_TICKS_PER_SECOND / 10u;

/**
  * @brief  Maximum reading age expressed in consumer ticks (500 ms).
  */
static uint32_t age_limit_ticks(uint32_t ticks_per_second)
{
  return (uint32_t)((((uint64_t)GNSS_VCP_MPU_MAX_DATA_AGE_MS) * ticks_per_second) / 1000u);
}

static void init_shared(GnssVcpMpuStatus *shared)
{
  memset(shared, 0, sizeof(*shared));
}

/**
  * @brief  Publishes a valid MPU-9250 reading (heading available) the way CPU1
  *         does, keeping the seqlock running so every call is a new publication.
  */
static void publish_9250(GnssVcpMpuStatus *shared, uint32_t producer_stamp_ms)
{
  GNSS_VCP_PublishMpuStatus(shared, producer_stamp_ms, 1u, 1u,
                            GNSS_VCP_MPU_MODEL_9250,
                            REF_HEADING, REF_ROLL, REF_PITCH);
}

/**
  * @brief  Publishes a valid MPU-6500 reading: roll/pitch valid, heading
  *         explicitly unavailable and zero.
  */
static void publish_6500(GnssVcpMpuStatus *shared, uint32_t producer_stamp_ms)
{
  GNSS_VCP_PublishMpuStatus(shared, producer_stamp_ms, 1u, 0u,
                            GNSS_VCP_MPU_MODEL_6500,
                            0, REF_ROLL, REF_PITCH);
}

/**
  * @brief  Publishes an invalid reading (sensor read failed): no data, but the
  *         detected model is still reported.
  */
static void publish_invalid(GnssVcpMpuStatus *shared, uint32_t producer_stamp_ms)
{
  GNSS_VCP_PublishMpuStatus(shared, producer_stamp_ms, 0u, 0u,
                            GNSS_VCP_MPU_MODEL_9250,
                            0, 0, 0);
}

static void expect_observe(GnssVcpMpuStatus *shared, uint32_t now_ticks,
                           MpuSnapshotWatch *watch)
{
  MPU_STATUS_ObserveFreshness_Host(shared, now_ticks, CONSUMER_TICKS_PER_SECOND,
                                   watch);
}

static bool read_latest(GnssVcpMpuStatus *shared, uint32_t now_ticks,
                        MpuSnapshotWatch *watch, MpuTelemetryStatus *status)
{
  return MPU_STATUS_ReadLatest_Host(shared, now_ticks, CONSUMER_TICKS_PER_SECOND,
                                    watch, status);
}

/* The first coherent publication a consumer sees is only remembered: its age at
   that moment is unbounded, because CPU1 may have published it long before. */
static void check_first_observation_is_not_presented(void)
{
  GnssVcpMpuStatus shared;
  MpuSnapshotWatch watch = {0};
  MpuTelemetryStatus status;

  init_shared(&shared);
  publish_9250(&shared, PRODUCER_STAMP_MS);

  if (read_latest(&shared, CONSUMER_ORIGIN_TICKS, &watch, &status))
  {
    printf("FAIL first_observation: the first publication was presented\n");
    failures++;
  }
  if (status.valid != 0u || status.heading_valid != 0u)
  {
    printf("FAIL first_observation: invalid answer carried data\n");
    failures++;
  }
}

/* A publication that replaces the recorded one within one age window was
   published after a read of this core, so its age is bounded and it is
   presented - field for field, including heading validity and model. */
static void check_replaced_publication_is_presented(void)
{
  GnssVcpMpuStatus shared;
  MpuSnapshotWatch watch = {0};
  MpuTelemetryStatus status;
  uint32_t now_ticks = CONSUMER_ORIGIN_TICKS;
  unsigned int i;

  init_shared(&shared);
  publish_9250(&shared, PRODUCER_STAMP_MS);
  if (read_latest(&shared, now_ticks, &watch, &status))
  {
    printf("FAIL replaced_publication: the first publication was presented\n");
    failures++;
  }

  /* Observe for 4 intervals (408 ticks, within the 512-tick window). */
  for (i = 0u; i < 4u; i++)
  {
    now_ticks += FRESHNESS_INTERVAL_TICKS;
    expect_observe(&shared, now_ticks, &watch);
  }

  /* CPU1 publishes a new reading within the window. */
  now_ticks += FRESHNESS_INTERVAL_TICKS;
  publish_9250(&shared, PRODUCER_STAMP_MS + 400u);
  expect_observe(&shared, now_ticks, &watch);

  if (!read_latest(&shared, now_ticks, &watch, &status))
  {
    printf("FAIL replaced_publication: the replacement was not presented\n");
    failures++;
  }
  if (status.valid != 1u || status.heading_valid != 1u)
  {
    printf("FAIL replaced_publication: valid=%u heading_valid=%u, expected 1/1\n",
           (unsigned)status.valid, (unsigned)status.heading_valid);
    failures++;
  }
  if (status.model != GNSS_VCP_MPU_MODEL_9250)
  {
    printf("FAIL replaced_publication: model=%u, expected %u\n",
           (unsigned)status.model, (unsigned)GNSS_VCP_MPU_MODEL_9250);
    failures++;
  }
  if (status.heading != REF_HEADING || status.roll != REF_ROLL || status.pitch != REF_PITCH)
  {
    printf("FAIL replaced_publication: angles (%d,%d,%d) mismatch\n",
           status.heading, status.roll, status.pitch);
    failures++;
  }
}

/* An MPU-6500 publishes roll/pitch with heading_valid clear and heading zero.
   The consumer must present the orientation while never fabricating a heading,
   i.e. it must not derive heading validity from the orientation validity. */
static void check_heading_validity_not_conflated(void)
{
  GnssVcpMpuStatus shared;
  MpuSnapshotWatch watch = {0};
  MpuTelemetryStatus status;
  uint32_t now_ticks = CONSUMER_ORIGIN_TICKS;
  unsigned int i;

  init_shared(&shared);
  publish_6500(&shared, PRODUCER_STAMP_MS);
  (void)read_latest(&shared, now_ticks, &watch, &status);

  for (i = 0u; i < 4u; i++)
  {
    now_ticks += FRESHNESS_INTERVAL_TICKS;
    expect_observe(&shared, now_ticks, &watch);
  }
  now_ticks += FRESHNESS_INTERVAL_TICKS;
  publish_6500(&shared, PRODUCER_STAMP_MS + 400u);
  expect_observe(&shared, now_ticks, &watch);

  if (!read_latest(&shared, now_ticks, &watch, &status))
  {
    printf("FAIL heading_unavailable: the orientation was not presented\n");
    failures++;
  }
  if (status.valid != 1u)
  {
    printf("FAIL heading_unavailable: valid=%u, expected 1\n", (unsigned)status.valid);
    failures++;
  }
  if (status.heading_valid != 0u)
  {
    printf("FAIL heading_unavailable: heading_valid=%u, expected 0\n",
           (unsigned)status.heading_valid);
    failures++;
  }
  if (status.heading != 0)
  {
    printf("FAIL heading_unavailable: heading=%d, expected 0\n", status.heading);
    failures++;
  }
  if (status.roll != REF_ROLL || status.pitch != REF_PITCH)
  {
    printf("FAIL heading_unavailable: roll/pitch (%d,%d) lost\n",
           status.roll, status.pitch);
    failures++;
  }
  if (status.model != GNSS_VCP_MPU_MODEL_6500)
  {
    printf("FAIL heading_unavailable: model=%u, expected %u\n",
           (unsigned)status.model, (unsigned)GNSS_VCP_MPU_MODEL_6500);
    failures++;
  }
}

/* The reading expires 500 ms after the observation that dated it, on the
   consumer's own clock, and stays expired while the publication is frozen. */
static void check_500ms_expiration(void)
{
  GnssVcpMpuStatus shared;
  MpuSnapshotWatch watch = {0};
  MpuTelemetryStatus status;
  uint32_t limit = age_limit_ticks(CONSUMER_TICKS_PER_SECOND);
  uint32_t now_ticks = CONSUMER_ORIGIN_TICKS;
  unsigned int i;

  /* The limit must be 512 ticks = 500 ms at 1024 Hz. */
  if (limit != 512u)
  {
    printf("FAIL expiration: 500 ms limit is %u ticks, expected 512\n", limit);
    failures++;
  }

  init_shared(&shared);
  publish_9250(&shared, PRODUCER_STAMP_MS);
  (void)read_latest(&shared, now_ticks, &watch, &status);

  for (i = 0u; i < 4u; i++)
  {
    now_ticks += FRESHNESS_INTERVAL_TICKS;
    expect_observe(&shared, now_ticks, &watch);
  }
  now_ticks += FRESHNESS_INTERVAL_TICKS;
  publish_9250(&shared, PRODUCER_STAMP_MS + 400u);
  expect_observe(&shared, now_ticks, &watch);

  /* Present at the observation time and exactly at the limit. */
  if (!read_latest(&shared, now_ticks, &watch, &status))
  {
    printf("FAIL expiration: the fresh reading was not presented\n");
    failures++;
  }
  if (!read_latest(&shared, now_ticks + limit - 1u, &watch, &status))
  {
    printf("FAIL expiration: the reading expired one tick early\n");
    failures++;
  }
  /* One tick past the limit: the frozen reading must not travel. */
  if (read_latest(&shared, now_ticks + limit + 1u, &watch, &status))
  {
    printf("FAIL expiration: the reading was presented past 500 ms\n");
    failures++;
  }
  if (status.valid != 0u || status.heading_valid != 0u)
  {
    printf("FAIL expiration: the expired answer carried data\n");
    failures++;
  }
  /* Expiry is sticky for the same frozen publication. */
  if (read_latest(&shared, now_ticks + (4u * limit), &watch, &status))
  {
    printf("FAIL expiration: the expired reading was presented again\n");
    failures++;
  }
}

/* A published invalid reading is never turned into a valid one, and the model
   of the detected device is still reported on the invalid answer. */
static void check_published_invalid(void)
{
  GnssVcpMpuStatus shared;
  MpuSnapshotWatch watch = {0};
  MpuTelemetryStatus status;
  uint32_t now_ticks = CONSUMER_ORIGIN_TICKS;
  unsigned int i;

  init_shared(&shared);
  publish_invalid(&shared, PRODUCER_STAMP_MS);
  (void)read_latest(&shared, now_ticks, &watch, &status);

  for (i = 0u; i < 4u; i++)
  {
    now_ticks += FRESHNESS_INTERVAL_TICKS;
    expect_observe(&shared, now_ticks, &watch);
  }
  now_ticks += FRESHNESS_INTERVAL_TICKS;
  publish_invalid(&shared, PRODUCER_STAMP_MS + 400u);
  expect_observe(&shared, now_ticks, &watch);

  if (read_latest(&shared, now_ticks, &watch, &status))
  {
    printf("FAIL published_invalid: an invalid reading was presented\n");
    failures++;
  }
  if (status.valid != 0u || status.heading != 0 || status.roll != 0 || status.pitch != 0)
  {
    printf("FAIL published_invalid: invalid answer carried data\n");
    failures++;
  }
}

/* An uninitialised region (random RAM / CPU1 has not set it up) is never a
   reading, even if the bytes happen to look like one. */
static void check_uninitialised_snapshot(void)
{
  GnssVcpMpuStatus shared;
  MpuSnapshotWatch watch = {0};
  MpuTelemetryStatus status;

  init_shared(&shared);
  if (read_latest(&shared, CONSUMER_ORIGIN_TICKS, &watch, &status))
  {
    printf("FAIL uninitialised: a zeroed region was presented\n");
    failures++;
  }

  memset(&shared, 0xFF, sizeof(shared));
  shared.seq = 2u;                 /* even, so the seqlock copy would succeed */
  shared.initialized = 0x00u;      /* not GNSS_VCP_MPU_STATUS_INIT_MARK */
  shared.valid = 1u;
  shared.heading_valid = 1u;
  shared.model = GNSS_VCP_MPU_MODEL_9250;
  shared.heading = REF_HEADING;
  shared.roll = REF_ROLL;
  shared.pitch = REF_PITCH;

  if (read_latest(&shared, CONSUMER_ORIGIN_TICKS + CONSUMER_TICKS_PER_SECOND,
                  &watch, &status))
  {
    printf("FAIL uninitialised: an unmarked region was presented\n");
    failures++;
  }
}

int main(void)
{
  check_first_observation_is_not_presented();
  check_replaced_publication_is_presented();
  check_heading_validity_not_conflated();
  check_500ms_expiration();
  check_published_invalid();
  check_uninitialised_snapshot();

  if (failures != 0u)
  {
    printf("%u failure(s)\n", failures);
    return 1;
  }

  printf("telemetry_mpu_status: all tests passed\n");
  return 0;
}
