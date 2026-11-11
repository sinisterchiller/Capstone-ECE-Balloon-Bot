/*
 * Host tests for Common/Telemetry/telemetry_gps_status.c.
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
 * stamps fixes with its SysTick milliseconds starting at its own reset, CPU2
 * (the consumer) counts RTC ticks at 1024 Hz from the free-running RTC
 * counter. Origins and rates therefore have no relation, and no arithmetic may
 * mix them.
 *
 * Build and run from the MXproject directory:
 *
 *   cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
 *      -DGNSS_VCP_BARRIER_HOST \
 *      -I Common/Telemetry -I Common/GnssVcp \
 *      Common/Telemetry/telemetry.c \
 *      Common/Telemetry/telemetry_gps_status.c \
 *      Common/Telemetry/telemetry_gps_status_test.c \
 *      -o /tmp/telemetry_gps_status_test && /tmp/telemetry_gps_status_test
 */

#include "telemetry_gps_status.h"

#include <stdio.h>
#include <string.h>

static unsigned int failures;

/* CPU1 SysTick milliseconds when the reference fix was validated. */
static const uint32_t PRODUCER_STAMP_MS = 1000u;
static const int32_t FIX_LATITUDE_UDEG = 48117300;
static const int32_t FIX_LONGITUDE_UDEG = 11516667;
static const int32_t FIX_ALTITUDE_CM = 54540;
static const uint8_t FIX_SATELLITES = 8u;

/* CPU2 counts RTC ticks: 1024 per second in binary mode. */
static const uint32_t CONSUMER_TICKS_PER_SECOND = 1024u;
/* A consumer counter origin that is unrelated to CPU1's: the RTC counter can
   hold any value when the application starts. */
static const uint32_t CONSUMER_ORIGIN_TICKS = 0x40000000u;
/* The payload's telemetry interval: the consumer reads the status this often. */
static const uint32_t TELEMETRY_INTERVAL_TICKS = 10u * CONSUMER_TICKS_PER_SECOND;

/**
  * @brief  Maximum fix age expressed in consumer ticks.
  */
static uint32_t age_limit_ticks(uint32_t ticks_per_second)
{
  return (uint32_t)((((uint64_t)TELEMETRY_MAX_FIX_AGE_MS) * ticks_per_second) / 1000u);
}

/**
  * @brief  Clears the shared status, as an uninitialised region would be.
  */
static void init_shared(GnssVcpGpsStatus *shared)
{
  memset(shared, 0, sizeof(*shared));
}

/**
  * @brief  Publishes a valid fix the way CPU1 does, keeping the seqlock running
  *         so that every call is a new publication.
  */
static void publish_fix(GnssVcpGpsStatus *shared, uint32_t producer_stamp_ms,
                        int32_t latitude_udeg)
{
  GNSS_VCP_PublishGpsStatus(shared, producer_stamp_ms, 1u, latitude_udeg,
                            FIX_LONGITUDE_UDEG, FIX_ALTITUDE_CM, FIX_SATELLITES);
}

/**
  * @brief  Publishes a no-fix status the way CPU1 does.
  */
static void publish_no_fix(GnssVcpGpsStatus *shared)
{
  GNSS_VCP_PublishGpsStatus(shared, PRODUCER_STAMP_MS, 0u, 0, 0, 0, 7u);
}

/**
  * @brief  Publishes nothing but marks the region set up, as GNSS_VCP_Init()
  *         leaves it before the first GGA arrives.
  */
static void init_shared_published_nothing(GnssVcpGpsStatus *shared)
{
  init_shared(shared);
  shared->initialized = GNSS_VCP_GPS_STATUS_INIT_MARK;
}

/**
  * @brief  Checks that a status carries no fix and no coordinates.
  */
static void check_is_no_fix(const char *name, const TelemetryGpsStatus *status)
{
  if (status->fix_valid != 0u)
  {
    printf("FAIL %s: fix_valid is %u, expected 0\n", name, (unsigned int)status->fix_valid);
    failures++;
  }
  if ((status->latitude_udeg != 0) || (status->longitude_udeg != 0) ||
      (status->altitude_cm != 0) || (status->satellites_used != 0u))
  {
    printf("FAIL %s: no-fix status kept coordinates (%ld, %ld, %ld, %u)\n", name,
           (long)status->latitude_udeg, (long)status->longitude_udeg,
           (long)status->altitude_cm, (unsigned int)status->satellites_used);
    failures++;
  }
}

/**
  * @brief  Checks that a status carries the reference fix.
  */
static void check_is_fix(const char *name, const TelemetryGpsStatus *status,
                         int32_t expected_latitude_udeg)
{
  if (status->fix_valid != 1u)
  {
    printf("FAIL %s: fix_valid is %u, expected 1\n", name, (unsigned int)status->fix_valid);
    failures++;
  }
  if (status->latitude_udeg != expected_latitude_udeg)
  {
    printf("FAIL %s: latitude %ld, expected %ld\n", name,
           (long)status->latitude_udeg, (long)expected_latitude_udeg);
    failures++;
  }
  if (status->longitude_udeg != FIX_LONGITUDE_UDEG)
  {
    printf("FAIL %s: longitude %ld, expected %ld\n", name,
           (long)status->longitude_udeg, (long)FIX_LONGITUDE_UDEG);
    failures++;
  }
  if (status->altitude_cm != FIX_ALTITUDE_CM)
  {
    printf("FAIL %s: altitude %ld, expected %ld\n", name,
           (long)status->altitude_cm, (long)FIX_ALTITUDE_CM);
    failures++;
  }
  if (status->satellites_used != FIX_SATELLITES)
  {
    printf("FAIL %s: satellites %u, expected %u\n", name,
           (unsigned int)status->satellites_used, (unsigned int)FIX_SATELLITES);
    failures++;
  }
}

/**
  * @brief  Reads once at the consumer's RTC rate and checks the fix answer.
  * @note   A no-fix answer must also carry no coordinates, i.e. an unusable
  *         publication never leaks the previous contents.
  */
static void expect_read(const char *name, bool expect_fix,
                        GnssVcpGpsStatus *shared, uint32_t now_ticks,
                        TelemetryGpsSnapshotWatch *watch, TelemetryGpsStatus *status)
{
  bool current = Telemetry_ReadGpsStatusSnapshot(shared, now_ticks,
                                                CONSUMER_TICKS_PER_SECOND,
                                                watch, status);

  if (current != expect_fix)
  {
    printf("FAIL %s: read at %lu ticks returned fix=%d, expected %d\n", name,
           (unsigned long)now_ticks, (int)current, (int)expect_fix);
    failures++;
  }

  if (expect_fix)
  {
    if (status->fix_valid != 1u)
    {
      printf("FAIL %s: a fix was expected but fix_valid is %u\n", name,
             (unsigned int)status->fix_valid);
      failures++;
    }
  }
  else
  {
    check_is_no_fix(name, status);
  }
}

/**
  * @brief  Checks that the tracker holds the publication of this read.
  */
static void check_watch(const char *name, const TelemetryGpsSnapshotWatch *watch,
                        uint32_t expected_sequence, uint32_t expected_ticks,
                        bool expected_change_seen)
{
  if (watch->observed != true)
  {
    printf("FAIL %s: the publication was not recorded\n", name);
    failures++;
  }
  if (watch->sequence != expected_sequence)
  {
    printf("FAIL %s: recorded publication %lu, expected %lu\n", name,
           (unsigned long)watch->sequence, (unsigned long)expected_sequence);
    failures++;
  }
  if (watch->observed_ticks != expected_ticks)
  {
    printf("FAIL %s: observation at %lu, expected %lu\n", name,
           (unsigned long)watch->observed_ticks, (unsigned long)expected_ticks);
    failures++;
  }
  if (watch->change_seen != expected_change_seen)
  {
    printf("FAIL %s: change_seen %d, expected %d\n", name, (int)watch->change_seen,
           (int)expected_change_seen);
    failures++;
  }
}

/* The first coherent publication a consumer sees is only remembered: its age at
   that moment is unbounded, because CPU1 may have published it long before. */
static void check_first_observation_is_not_presented(void)
{
  GnssVcpGpsStatus shared;
  TelemetryGpsSnapshotWatch watch = {0};
  TelemetryGpsStatus status;

  init_shared(&shared);
  publish_fix(&shared, PRODUCER_STAMP_MS, FIX_LATITUDE_UDEG);

  expect_read("first_observation", false, &shared, CONSUMER_ORIGIN_TICKS, &watch,
              &status);
  check_watch("first_observation", &watch, shared.seq, CONSUMER_ORIGIN_TICKS, false);
}

/* The recorded publication is not presented either while it stays unreplaced:
   the elapsed counter value still bounds nothing. This is the case of CPU2
   starting after CPU1 halted with a valid fix left in the shared status. */
static void check_unreplaced_publication_is_not_presented(void)
{
  GnssVcpGpsStatus shared;
  TelemetryGpsSnapshotWatch watch = {0};
  TelemetryGpsStatus status;
  uint32_t limit = age_limit_ticks(CONSUMER_TICKS_PER_SECOND);
  uint32_t now_ticks;
  unsigned int read;

  /* CPU1 halted two hours ago; its last publication claimed a valid fix. */
  init_shared(&shared);
  publish_fix(&shared, 7200000u, FIX_LATITUDE_UDEG);

  /* Two minutes of telemetry intervals after CPU2 starts: not one of them may
     carry those coordinates. */
  now_ticks = CONSUMER_ORIGIN_TICKS;
  for (read = 0u; read < 12u; read++)
  {
    expect_read("unreplaced_publication", false, &shared, now_ticks, &watch, &status);
    now_ticks += TELEMETRY_INTERVAL_TICKS;
  }
  check_watch("unreplaced_publication", &watch, shared.seq, CONSUMER_ORIGIN_TICKS, false);
  if (shared.fix_valid != 1u)
  {
    printf("FAIL unreplaced_publication: the frozen shared snapshot changed during "
           "the test\n");
    failures++;
  }

  /* Well past the age limit the answer stays no fix. */
  expect_read("unreplaced_publication_late", false, &shared,
              CONSUMER_ORIGIN_TICKS + (4u * limit), &watch, &status);
}

/* A CPU2 reset clears the observation (the tracker is zero-initialised in the
   restarting image): a fix CPU1 froze before that reset is not presented again,
   even though an earlier run of CPU2 presented it. */
static void check_consumer_reset_after_stopped_producer(void)
{
  GnssVcpGpsStatus shared;
  TelemetryGpsSnapshotWatch watch = {0};
  TelemetryGpsStatus status;
  uint32_t now_ticks = CONSUMER_ORIGIN_TICKS;

  /* A running pair: CPU2 observed the replacement and presented the fix. */
  init_shared(&shared);
  publish_fix(&shared, PRODUCER_STAMP_MS, FIX_LATITUDE_UDEG);
  expect_read("consumer_reset", false, &shared, now_ticks, &watch, &status);
  publish_fix(&shared, PRODUCER_STAMP_MS + 1000u, FIX_LATITUDE_UDEG);
  now_ticks += CONSUMER_TICKS_PER_SECOND;
  expect_read("consumer_reset", true, &shared, now_ticks, &watch, &status);

  /* CPU2 restarts while CPU1 is halted with that fix frozen in the shared
     status. */
  memset(&watch, 0, sizeof(watch));
  now_ticks += TELEMETRY_INTERVAL_TICKS;
  expect_read("consumer_reset", false, &shared, now_ticks, &watch, &status);
  now_ticks += TELEMETRY_INTERVAL_TICKS;
  expect_read("consumer_reset", false, &shared, now_ticks, &watch, &status);
}

/* A publication that replaces the recorded one within one age window was
   published after a read of this core, so its age is bounded and it is
   presented - field for field. */
static void check_replaced_publication_is_presented(void)
{
  GnssVcpGpsStatus shared;
  TelemetryGpsSnapshotWatch watch = {0};
  TelemetryGpsStatus status;
  uint32_t now_ticks = CONSUMER_ORIGIN_TICKS;

  init_shared(&shared);
  publish_fix(&shared, PRODUCER_STAMP_MS, FIX_LATITUDE_UDEG);
  expect_read("replaced_publication", false, &shared, now_ticks, &watch, &status);

  /* CPU1 publishes the next GGA one second later. */
  now_ticks += CONSUMER_TICKS_PER_SECOND;
  publish_fix(&shared, PRODUCER_STAMP_MS + 1000u, FIX_LATITUDE_UDEG + 1);
  expect_read("replaced_publication", true, &shared, now_ticks, &watch, &status);
  check_is_fix("replaced_publication", &status, FIX_LATITUDE_UDEG + 1);
  check_watch("replaced_publication", &watch, shared.seq, now_ticks, true);

  if (status.fix_timestamp_ms != (PRODUCER_STAMP_MS + 1000u))
  {
    printf("FAIL replaced_publication: producer stamp %lu, expected %lu\n",
           (unsigned long)status.fix_timestamp_ms,
           (unsigned long)(PRODUCER_STAMP_MS + 1000u));
    failures++;
  }
  if (status.fix_sequence != shared.seq)
  {
    printf("FAIL replaced_publication: fix_sequence %lu, expected publication %lu\n",
           (unsigned long)status.fix_sequence, (unsigned long)shared.seq);
    failures++;
  }
}

/* The producer's stamp and the consumer's counter are not comparable: a
   consumer counter below the producer's stamp must not turn a fresh fix into a
   stale one, and one above it must not turn a frozen fix into a current one.
   This is the regression test for the cross-clock subtraction defect. */
static void check_producer_stamp_not_subtracted_from_consumer_time(void)
{
  GnssVcpGpsStatus shared;
  TelemetryGpsSnapshotWatch watch = {0};
  TelemetryGpsStatus status;
  uint32_t limit = age_limit_ticks(CONSUMER_TICKS_PER_SECOND);
  uint32_t accepted_ticks;

  /* Consumer counter far below the producer stamp (as after a CPU2 reset while
     the RTC counter is small and CPU1 has been running for a while). */
  init_shared(&shared);
  publish_fix(&shared, 3600000u /* CPU1 ran for an hour */, FIX_LATITUDE_UDEG);
  expect_read("stamp_not_subtracted", false, &shared, 512u, &watch, &status);

  /* The publication that replaces it is dated by this core's counter, so a
     producer stamp far above that counter cannot expire it. */
  accepted_ticks = 512u + CONSUMER_TICKS_PER_SECOND;
  publish_fix(&shared, 3601000u, FIX_LATITUDE_UDEG);
  expect_read("stamp_not_subtracted", true, &shared, accepted_ticks, &watch, &status);

  /* A frozen publication must expire on the consumer's clock even though the
     producer's stamp is far away from the consumer's counter. */
  expect_read("stamp_not_subtracted", false, &shared, accepted_ticks + limit + 1u,
              &watch, &status);
}

/* The consumer's counter rate is a parameter, not an assumption: the same
   publication must expire after TELEMETRY_MAX_FIX_AGE_MS on a 1000 Hz counter
   and on a 1024 Hz counter, and the age limit must follow the rate. */
static void check_age_follows_consumer_tick_rate(void)
{
  static const uint32_t rates[] = {1000u, 1024u, 32768u};
  GnssVcpGpsStatus shared;
  unsigned int i;

  for (i = 0u; i < (sizeof(rates) / sizeof(rates[0])); i++)
  {
    TelemetryGpsSnapshotWatch watch = {0};
    TelemetryGpsStatus status;
    uint32_t rate = rates[i];
    uint32_t limit = age_limit_ticks(rate);

    init_shared(&shared);
    publish_fix(&shared, PRODUCER_STAMP_MS, FIX_LATITUDE_UDEG);

    /* First observation: recorded, never presented. */
    if (Telemetry_ReadGpsStatusSnapshot(&shared, CONSUMER_ORIGIN_TICKS, rate,
                                        &watch, &status))
    {
      printf("FAIL tick_rate(%lu): the first observation was presented\n",
             (unsigned long)rate);
      failures++;
    }

    /* One age window later a replacement is still within the limit. */
    publish_fix(&shared, PRODUCER_STAMP_MS + 1000u, FIX_LATITUDE_UDEG);
    if (!Telemetry_ReadGpsStatusSnapshot(&shared, CONSUMER_ORIGIN_TICKS + limit, rate,
                                         &watch, &status))
    {
      printf("FAIL tick_rate(%lu): the fix at the age limit was rejected\n",
             (unsigned long)rate);
      failures++;
    }
    if (Telemetry_ReadGpsStatusSnapshot(&shared, CONSUMER_ORIGIN_TICKS + (2u * limit) + 1u,
                                        rate, &watch, &status))
    {
      printf("FAIL tick_rate(%lu): the fix past the age limit was accepted\n",
             (unsigned long)rate);
      failures++;
    }
    check_is_no_fix("tick_rate", &status);
  }
}

/* CPU1 stops publishing (halted, faulted, reset): the snapshot still says
   "valid fix", but the consumer expires it on its own clock. */
static void check_stopped_producer_expires(void)
{
  GnssVcpGpsStatus shared;
  TelemetryGpsSnapshotWatch watch = {0};
  TelemetryGpsStatus status;
  uint32_t limit = age_limit_ticks(CONSUMER_TICKS_PER_SECOND);

  init_shared(&shared);
  publish_fix(&shared, PRODUCER_STAMP_MS, FIX_LATITUDE_UDEG);
  expect_read("stopped_producer", false, &shared, CONSUMER_ORIGIN_TICKS, &watch,
              &status);

  publish_fix(&shared, PRODUCER_STAMP_MS + 1000u, FIX_LATITUDE_UDEG);
  expect_read("stopped_producer", true, &shared, CONSUMER_ORIGIN_TICKS + 1u, &watch,
              &status);

  /* Still current at the age limit, with the snapshot untouched by CPU1. */
  expect_read("stopped_producer", true, &shared, CONSUMER_ORIGIN_TICKS + 1u + limit,
              &watch, &status);
  check_is_fix("stopped_producer", &status, FIX_LATITUDE_UDEG);

  /* One tick past the limit: the stale coordinates must not travel. */
  expect_read("stopped_producer", false, &shared,
              CONSUMER_ORIGIN_TICKS + 2u + limit, &watch, &status);
  if (shared.fix_valid != 1u)
  {
    printf("FAIL stopped_producer: the frozen shared snapshot changed during the test\n");
    failures++;
  }

  /* Expiry is sticky: later reads of the same frozen publication stay expired. */
  expect_read("stopped_producer_late", false, &shared,
              CONSUMER_ORIGIN_TICKS + (4u * limit), &watch, &status);
}

/* A fix that is still being refreshed by CPU1 stays current across reads, and a
   new publication restarts the age window. */
static void check_new_publication_restarts_age(void)
{
  GnssVcpGpsStatus shared;
  TelemetryGpsSnapshotWatch watch = {0};
  TelemetryGpsStatus status;
  uint32_t now_ticks;
  uint32_t limit = age_limit_ticks(CONSUMER_TICKS_PER_SECOND);

  init_shared(&shared);
  publish_fix(&shared, PRODUCER_STAMP_MS, FIX_LATITUDE_UDEG);
  expect_read("new_publication", false, &shared, CONSUMER_ORIGIN_TICKS, &watch,
              &status);

  now_ticks = CONSUMER_ORIGIN_TICKS + 1u;
  publish_fix(&shared, PRODUCER_STAMP_MS + 1000u, FIX_LATITUDE_UDEG);
  expect_read("new_publication", true, &shared, now_ticks, &watch, &status);

  /* Same publication, read again half way through its age window. */
  now_ticks += limit / 2u;
  expect_read("new_publication", true, &shared, now_ticks, &watch, &status);

  /* CPU1 published a new GGA fix before this read: new coordinates, and the
     age window starts again from this record. */
  publish_fix(&shared, PRODUCER_STAMP_MS + 31000u, FIX_LATITUDE_UDEG + 1);
  expect_read("new_publication", true, &shared, now_ticks, &watch, &status);
  check_is_fix("new_publication", &status, FIX_LATITUDE_UDEG + 1);

  now_ticks += limit;
  expect_read("new_publication", true, &shared, now_ticks, &watch, &status);
  now_ticks += 1u;
  expect_read("new_publication", false, &shared, now_ticks, &watch, &status);
}

/* Reading twice at the same consumer time must not push the age window
   forward. */
static void check_repeated_read_does_not_refresh_age(void)
{
  GnssVcpGpsStatus shared;
  TelemetryGpsSnapshotWatch watch = {0};
  TelemetryGpsStatus status;
  uint32_t limit = age_limit_ticks(CONSUMER_TICKS_PER_SECOND);

  init_shared(&shared);
  publish_fix(&shared, PRODUCER_STAMP_MS, FIX_LATITUDE_UDEG);
  expect_read("repeated_read", false, &shared, CONSUMER_ORIGIN_TICKS, &watch, &status);
  publish_fix(&shared, PRODUCER_STAMP_MS + 1000u, FIX_LATITUDE_UDEG);
  expect_read("repeated_read", true, &shared, CONSUMER_ORIGIN_TICKS, &watch, &status);
  expect_read("repeated_read", true, &shared, CONSUMER_ORIGIN_TICKS, &watch, &status);

  expect_read("repeated_read", false, &shared, CONSUMER_ORIGIN_TICKS + limit + 1u,
              &watch, &status);
}

/* The consumer's counter wraps: CPU2's RTC tick counter wraps after about
   48 days, and the age must survive that wrap. */
static void check_consumer_clock_wraparound(void)
{
  GnssVcpGpsStatus shared;
  TelemetryGpsSnapshotWatch watch = {0};
  TelemetryGpsStatus status;

  init_shared(&shared);
  publish_fix(&shared, PRODUCER_STAMP_MS, FIX_LATITUDE_UDEG);

  /* 512 ticks before the wrap. */
  expect_read("consumer_wrap", false, &shared, 0xFFFFFE00u, &watch, &status);

  /* A replacement published 512 ticks later, across the wrap. */
  publish_fix(&shared, PRODUCER_STAMP_MS + 1000u, FIX_LATITUDE_UDEG);
  expect_read("consumer_wrap", true, &shared, 0x00000200u, &watch, &status);
  check_is_fix("consumer_wrap", &status, FIX_LATITUDE_UDEG);

  /* 0x8000 ticks after the first observation: past the limit. */
  expect_read("consumer_wrap", false, &shared, 0x00008000u, &watch, &status);
}

/* A replacement noticed only after a whole age window has passed cannot be
   dated either: it is recorded and the next replacement is presented. */
static void check_change_after_gap_needs_another_change(void)
{
  GnssVcpGpsStatus shared;
  TelemetryGpsSnapshotWatch watch = {0};
  TelemetryGpsStatus status;
  uint32_t limit = age_limit_ticks(CONSUMER_TICKS_PER_SECOND);
  uint32_t now_ticks = CONSUMER_ORIGIN_TICKS;

  init_shared(&shared);
  publish_fix(&shared, PRODUCER_STAMP_MS, FIX_LATITUDE_UDEG);
  expect_read("gap", false, &shared, now_ticks, &watch, &status);

  /* The consumer was blocked for longer than the maximum age (the radio was
     retransmitting) while CPU1 kept publishing. */
  now_ticks += limit + 1u;
  publish_fix(&shared, PRODUCER_STAMP_MS + 31000u, FIX_LATITUDE_UDEG);
  expect_read("gap", false, &shared, now_ticks, &watch, &status);
  check_watch("gap", &watch, shared.seq, now_ticks, false);

  /* The next interval sees another publication, and this one is dated by the
     read of the gap. */
  now_ticks += TELEMETRY_INTERVAL_TICKS;
  publish_fix(&shared, PRODUCER_STAMP_MS + 41000u, FIX_LATITUDE_UDEG + 2);
  expect_read("gap", true, &shared, now_ticks, &watch, &status);
  check_is_fix("gap", &status, FIX_LATITUDE_UDEG + 2);
}

/* The payload's normal path: CPU1 publishes a GGA every second, CPU2 reads once
   per 10 s telemetry interval. The first interval after a CPU2 start carries no
   fix, every later one carries the latest fix - repeated publications must not
   starve the consumer and must not push the age window. */
static void check_normal_ten_second_telemetry_path(void)
{
  GnssVcpGpsStatus shared;
  TelemetryGpsSnapshotWatch watch = {0};
  TelemetryGpsStatus status;
  uint32_t now_ticks = CONSUMER_ORIGIN_TICKS;
  uint32_t producer_stamp_ms = PRODUCER_STAMP_MS;
  unsigned int interval;
  unsigned int gga;

  /* CPU1 already ran for a while when CPU2 starts. */
  init_shared(&shared);
  for (gga = 0u; gga < 5u; gga++)
  {
    producer_stamp_ms += 1000u;
    publish_fix(&shared, producer_stamp_ms, FIX_LATITUDE_UDEG);
  }

  expect_read("ten_second_path", false, &shared, now_ticks, &watch, &status);

  for (interval = 1u; interval <= 6u; interval++)
  {
    /* Ten GGA sentences (1 Hz) between two telemetry intervals. */
    for (gga = 0u; gga < 10u; gga++)
    {
      producer_stamp_ms += 1000u;
      publish_fix(&shared, producer_stamp_ms, FIX_LATITUDE_UDEG + (int32_t)interval);
    }
    now_ticks += TELEMETRY_INTERVAL_TICKS;

    expect_read("ten_second_path", true, &shared, now_ticks, &watch, &status);
    check_is_fix("ten_second_path", &status, FIX_LATITUDE_UDEG + (int32_t)interval);
  }
}

/* A published no-fix status is never turned into a fix, and it does not cost an
   extra interval: the first fix published after it is presented. */
static void check_published_no_fix(void)
{
  GnssVcpGpsStatus shared;
  TelemetryGpsSnapshotWatch watch = {0};
  TelemetryGpsStatus status;

  init_shared(&shared);
  publish_no_fix(&shared);

  expect_read("published_no_fix", false, &shared, CONSUMER_ORIGIN_TICKS, &watch,
              &status);

  publish_fix(&shared, PRODUCER_STAMP_MS + 1000u, FIX_LATITUDE_UDEG);
  expect_read("published_no_fix", true, &shared,
              CONSUMER_ORIGIN_TICKS + CONSUMER_TICKS_PER_SECOND, &watch, &status);
  check_is_fix("published_no_fix", &status, FIX_LATITUDE_UDEG);
}

/* GNSS_VCP_Init() marks the region set up while the sequence is still zero:
   that observation must count as a real one, so the first GGA that publishes a
   fix is presented without losing an interval. */
static void check_initialised_before_first_publication(void)
{
  GnssVcpGpsStatus shared;
  TelemetryGpsSnapshotWatch watch = {0};
  TelemetryGpsStatus status;

  init_shared_published_nothing(&shared);
  expect_read("init_before_publication", false, &shared, CONSUMER_ORIGIN_TICKS,
              &watch, &status);
  check_watch("init_before_publication", &watch, 0u, CONSUMER_ORIGIN_TICKS, false);

  publish_fix(&shared, PRODUCER_STAMP_MS, FIX_LATITUDE_UDEG);
  expect_read("init_before_publication", true, &shared,
              CONSUMER_ORIGIN_TICKS + CONSUMER_TICKS_PER_SECOND, &watch, &status);
  check_is_fix("init_before_publication", &status, FIX_LATITUDE_UDEG);
}

/* An unknown consumer clock rate cannot age anything: report no fix. */
static void check_unknown_tick_rate(void)
{
  GnssVcpGpsStatus shared;
  TelemetryGpsSnapshotWatch watch = {0};
  TelemetryGpsStatus status;

  init_shared(&shared);
  publish_fix(&shared, PRODUCER_STAMP_MS, FIX_LATITUDE_UDEG);

  if (Telemetry_ReadGpsStatusSnapshot(&shared, CONSUMER_ORIGIN_TICKS, 0u, &watch, &status))
  {
    printf("FAIL unknown_tick_rate: a fix was accepted with a zero tick rate\n");
    failures++;
  }
  check_is_no_fix("unknown_tick_rate", &status);
  if (watch.observed)
  {
    printf("FAIL unknown_tick_rate: the tracker was updated without a tick rate\n");
    failures++;
  }
}

/* An uninitialised snapshot is never a fix, and it forgets the observation: a
   CPU1 that restarted must not reuse the sequence numbers of the earlier run. */
static void check_uninitialised_snapshot(void)
{
  GnssVcpGpsStatus shared;
  TelemetryGpsSnapshotWatch watch = {0};
  TelemetryGpsStatus status;

  init_shared(&shared);
  expect_read("uninitialised", false, &shared, CONSUMER_ORIGIN_TICKS, &watch, &status);

  /* Power-up RAM that happens to look like a fresh fix, but carries no marker. */
  memset(&shared, 0xFF, sizeof(shared));
  shared.seq = 2u; /* even, so the seqlock copy would succeed */
  shared.initialized = 0x5Au; /* not GNSS_VCP_GPS_STATUS_INIT_MARK */
  shared.fix_valid = 1u;
  shared.fix_timestamp_ms = PRODUCER_STAMP_MS;
  shared.latitude_udeg = FIX_LATITUDE_UDEG;
  shared.longitude_udeg = FIX_LONGITUDE_UDEG;
  shared.altitude_cm = FIX_ALTITUDE_CM;
  shared.satellites_used = FIX_SATELLITES;

  expect_read("uninitialised_unmarked", false, &shared,
              CONSUMER_ORIGIN_TICKS + CONSUMER_TICKS_PER_SECOND, &watch, &status);
}

/* A region that loses its marker (CPU1 restarted from a cleared region, or lost
   power) rearms the consumer: the publication CPU1 creates next is remembered
   first and presented only once it is replaced. */
static void check_uninitialised_snapshot_rearms(void)
{
  GnssVcpGpsStatus shared;
  TelemetryGpsSnapshotWatch watch = {0};
  TelemetryGpsStatus status;
  uint32_t now_ticks = CONSUMER_ORIGIN_TICKS;

  init_shared(&shared);
  publish_fix(&shared, PRODUCER_STAMP_MS, FIX_LATITUDE_UDEG);
  expect_read("uninitialised_rearm", false, &shared, now_ticks, &watch, &status);
  publish_fix(&shared, PRODUCER_STAMP_MS + 1000u, FIX_LATITUDE_UDEG);
  now_ticks += CONSUMER_TICKS_PER_SECOND;
  expect_read("uninitialised_rearm", true, &shared, now_ticks, &watch, &status);

  /* CPU1 loses power: the shared region is cleared. */
  init_shared(&shared);
  now_ticks += CONSUMER_TICKS_PER_SECOND;
  expect_read("uninitialised_rearm", false, &shared, now_ticks, &watch, &status);
  if (watch.observed || watch.change_seen)
  {
    printf("FAIL uninitialised_rearm: the observation survived an uninitialised region\n");
    failures++;
  }

  /* CPU1 boots and publishes no fix, then a fix. */
  publish_no_fix(&shared);
  now_ticks += CONSUMER_TICKS_PER_SECOND;
  expect_read("uninitialised_rearm", false, &shared, now_ticks, &watch, &status);
  publish_fix(&shared, PRODUCER_STAMP_MS + 2000u, FIX_LATITUDE_UDEG);
  now_ticks += CONSUMER_TICKS_PER_SECOND;
  expect_read("uninitialised_rearm", true, &shared, now_ticks, &watch, &status);
}

/* CPU1 writing the status (odd seqlock) is not a fix, and never an old one; the
   completed write takes part in the normal observation rules afterwards. */
static void check_busy_snapshot(void)
{
  GnssVcpGpsStatus shared;
  TelemetryGpsSnapshotWatch watch = {0};
  TelemetryGpsStatus status;
  uint32_t now_ticks = CONSUMER_ORIGIN_TICKS;
  uint32_t busy_sequence;

  init_shared(&shared);
  publish_fix(&shared, PRODUCER_STAMP_MS, FIX_LATITUDE_UDEG);
  expect_read("busy_snapshot", false, &shared, now_ticks, &watch, &status);
  publish_fix(&shared, PRODUCER_STAMP_MS + 1000u, FIX_LATITUDE_UDEG);
  now_ticks += CONSUMER_TICKS_PER_SECOND;
  expect_read("busy_snapshot", true, &shared, now_ticks, &watch, &status);

  busy_sequence = shared.seq + 1u; /* CPU1 started writing and did not finish */
  shared.seq = busy_sequence;
  now_ticks += 1u;
  expect_read("busy_snapshot", false, &shared, now_ticks, &watch, &status);
  if (shared.seq != busy_sequence)
  {
    printf("FAIL busy_snapshot: the reader modified the shared sequence\n");
    failures++;
  }

  /* CPU1 finishes the write: the completed publication is dated by the read
     before it and is presented. */
  shared.seq += 1u;
  now_ticks += CONSUMER_TICKS_PER_SECOND;
  expect_read("busy_snapshot", true, &shared, now_ticks, &watch, &status);
}

/* Whatever the previous answer was, an unusable snapshot must not leak it. */
static void check_no_stale_leak_between_calls(void)
{
  GnssVcpGpsStatus shared;
  TelemetryGpsSnapshotWatch watch = {0};
  TelemetryGpsStatus status;

  init_shared(&shared);
  publish_fix(&shared, PRODUCER_STAMP_MS, FIX_LATITUDE_UDEG);
  expect_read("no_stale_leak", false, &shared, CONSUMER_ORIGIN_TICKS, &watch, &status);
  publish_fix(&shared, PRODUCER_STAMP_MS + 1000u, FIX_LATITUDE_UDEG);
  expect_read("no_stale_leak", true, &shared, CONSUMER_ORIGIN_TICKS + 1u, &watch,
              &status);

  init_shared(&shared);
  expect_read("no_stale_leak", false, &shared, CONSUMER_ORIGIN_TICKS + 2u, &watch,
              &status);
}

/* NULL arguments are reported as no fix instead of dereferenced. */
static void check_null_arguments(void)
{
  GnssVcpGpsStatus shared;
  TelemetryGpsSnapshotWatch watch = {0};
  TelemetryGpsStatus status;

  init_shared(&shared);
  publish_fix(&shared, PRODUCER_STAMP_MS, FIX_LATITUDE_UDEG);

  if (Telemetry_ReadGpsStatusSnapshot(NULL, CONSUMER_ORIGIN_TICKS,
                                      CONSUMER_TICKS_PER_SECOND, &watch, &status))
  {
    printf("FAIL null_arguments: a NULL snapshot was reported as a fix\n");
    failures++;
  }

  if (Telemetry_ReadGpsStatusSnapshot(&shared, CONSUMER_ORIGIN_TICKS,
                                      CONSUMER_TICKS_PER_SECOND, &watch, NULL))
  {
    printf("FAIL null_arguments: a NULL watch was reported as a fix\n");
    failures++;
  }

  if (Telemetry_ReadGpsStatusSnapshot(&shared, CONSUMER_ORIGIN_TICKS,
                                      CONSUMER_TICKS_PER_SECOND, &watch, NULL))
  {
    printf("FAIL null_arguments: a NULL destination was reported as a fix\n");
    failures++;
  }
}

int main(void)
{
  check_first_observation_is_not_presented();
  check_unreplaced_publication_is_not_presented();
  check_consumer_reset_after_stopped_producer();
  check_replaced_publication_is_presented();
  check_producer_stamp_not_subtracted_from_consumer_time();
  check_age_follows_consumer_tick_rate();
  check_stopped_producer_expires();
  check_new_publication_restarts_age();
  check_repeated_read_does_not_refresh_age();
  check_consumer_clock_wraparound();
  check_change_after_gap_needs_another_change();
  check_normal_ten_second_telemetry_path();
  check_published_no_fix();
  check_initialised_before_first_publication();
  check_unknown_tick_rate();
  check_uninitialised_snapshot();
  check_uninitialised_snapshot_rearms();
  check_busy_snapshot();
  check_no_stale_leak_between_calls();
  check_null_arguments();

  if (failures != 0u)
  {
    printf("%u failure(s)\n", failures);
    return 1;
  }

  printf("telemetry_gps_status: all tests passed\n");
  return 0;
}
