/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    mpu_status.c
  * @brief   Hands the shared MPU orientation status to the CPU2 telemetry
  *          application, together with the CPU2 time base the freshness check
  *          needs.
  *
  * The snapshot protocol and the no-data rules live in this file; it is the
  * consumer side used by the CPU2 telemetry application: it takes a coherent
  * copy of the snapshot and decides, at the moment of consumption, whether
  * the reading may still be presented as current.
  *
  * The two cores do not share a clock. CPU1 stamps a reading with its own
  * HAL_GetTick(), i.e. SysTick milliseconds counted from CPU1 reset, while
  * CPU2 counts RTC ticks at a different rate from the free-running RTC
  * counter (CM0PLUS/Core/Src/sys_app.c returns TIMER_IF_GetTimerValue()).
  * Subtracting a CPU1 stamp from a CPU2 counter is therefore meaningless.
  *
  * The age of a reading is consequently measured on the consumer's clock alone.
  * The consumer remembers which publication it last saw (the seqlock `seq` of
  * the snapshot) together with its own counter value when that publication
  * first appeared, and the age is the elapsed value of that same counter.
  * Counter and rate are passed by the caller, so the comparison with
  * GNSS_VCP_MPU_MAX_DATA_AGE_MS happens in one time domain and the modular
  * difference stays exact at the counter's own wrap. A new publication
  * restarts the measurement; a frozen one - CPU1 stopped, faulted, held in
  * reset or no longer publishing - keeps ageing and expires on its own.
  *
  * That measurement cannot date the first publication a consumer sees. CPU1
  * may have published it long before CPU2 booted, or left it behind when it
  * halted, so the elapsed counter value of the first read bounds nothing: an
  * old reading would look age zero. The first coherent publication is
  * therefore only remembered and never presented. A reading becomes
  * presentable once a read observes that publication replaced within one age
  * window, because that replacement was then published after a read of this
  * core. CPU2 sends no orientation data in the first telemetry interval after
  * a CPU2 (re)start and a reading from a later interval on, as soon as CPU1
  * publishes again; a snapshot inherited from an earlier run can never travel
  * as current, however old it is.
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "mpu_status.h"
#include "gnss_vcp_mailbox.h"

#if !defined(GNSS_VCP_BARRIER_HOST)
#include "main.h"
#else
#include <stddef.h>  /* for NULL */
#endif

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* Compile-time checks: verify the shared-memory layout matches the offsets
   used in gnss_vcp_mailbox.h. These must hold in every translation unit that
   includes the header, so the assertions are placed here (CM0+) and in
   CM4/Core/Src/gnss_vcp.c (CM4). */
_Static_assert(sizeof(GnssVcpMailbox) == 520u,
               "GnssVcpMailbox size must be 520 bytes (write + read + ring[512])");
_Static_assert(sizeof(GnssVcpGpsStatus) == 28u,
               "GnssVcpGpsStatus size must be 28 bytes");
_Static_assert(GNSS_VCP_GPS_STATUS_OFFSET == sizeof(GnssVcpMailbox),
               "GPS status offset must follow the mailbox ring");
_Static_assert(sizeof(GnssVcpMpuStatus) == 20u,
               "GnssVcpMpuStatus size must be 20 bytes");
_Static_assert(GNSS_VCP_MPU_STATUS_OFFSET ==
                 (GNSS_VCP_GPS_STATUS_OFFSET + sizeof(GnssVcpGpsStatus)),
               "MPU status offset must follow the GPS status");

#if !defined(GNSS_VCP_BARRIER_HOST)
/**
  * @brief Ticks per second of CPU2's HAL_GetTick() counter.
  * @note  sys_app.c overrides HAL_GetTick() with TIMER_IF_GetTimerValue(), so
  *        the value is the RTC binary-mode sub-second counter, not a
  *        millisecond count: it advances at LSE (32768 Hz) divided by the
  *        asynchronous prescaler (RTC_PREDIV_A + 1), i.e. 1 << RTC_N_PREDIV_S
  *        = 1024 ticks per second (CM0PLUS/Core/Src/rtc.c selects binary mode,
  *        main.h holds the prescaler). Its origin is whatever the RTC counter
  *        held when CPU2 started and is unrelated to CPU1's SysTick.
  */
#define MPU_STATUS_TICKS_PER_SECOND ((uint32_t)1u << RTC_N_PREDIV_S)
#else
/* Host test override: fixed 1024 ticks per second to match target. */
#define MPU_STATUS_TICKS_PER_SECOND 1024u
#endif

/**
  * @brief Attempts at a coherent seqlock read.
  * @note  CPU1 holds the status in the written state for a handful of stores
  *        and only when a new MPU poll lands, so a few immediate retries
  *        settle it. Giving up keeps the caller bounded: the radio task must
  *        never spin on the other core.
  */
#define MPU_STATUS_READ_ATTEMPTS 3u
/* USER CODE END PD */

/* Private functions ---------------------------------------------------------*/
/* USER CODE BEGIN PFP */

/**
  * @brief  Fills a status as "invalid": no validity, no data.
  * @param  status  Destination status.
  */
static void MPU_STATUS_SetInvalid(MpuTelemetryStatus *status)
{
  status->seq = 0u;
  status->update_timestamp_ms = 0u;
  status->valid = 0u;
  status->heading_valid = 0u;
  status->model = GNSS_VCP_MPU_MODEL_UNKNOWN;
  status->heading = 0;
  status->roll = 0;
  status->pitch = 0;
}

/**
  * @brief  Records the publication of this read as the one to age.
  * @param  watch  Tracker to update.
  * @param  sequence  seqlock sequence of the publication read.
  * @param  now_ticks  Reading core's counter at this read.
  * @param  change_seen  true when the publication was seen to replace an
  *                    earlier observed one within one age window, so that
  *                    `now_ticks` bounds its age.
  */
static void MPU_STATUS_RecordObservation(MpuSnapshotWatch *watch,
                                         uint32_t sequence,
                                         uint32_t now_ticks,
                                         bool change_seen)
{
  watch->sequence = sequence;
  watch->observed_ticks = now_ticks;
  watch->observed = true;
  watch->change_seen = change_seen;
}
/* USER CODE END PFP */

/* Exported functions --------------------------------------------------------*/
#if !defined(GNSS_VCP_BARRIER_HOST)
/* USER CODE BEGIN PV */
/**
  * @brief Publication of the CPU1 MPU status last observed by this core.
  * @note  Zeroed, i.e. "nothing observed yet", until the first read. It makes
  *        the reading age a difference of two values of this core's counter,
  *        so it must not be reset while the application runs.
  */
static MpuSnapshotWatch mpu_snapshot_watch = {0};
/* USER CODE END PV */

bool MPU_STATUS_ReadLatest(MpuTelemetryStatus *status)
{
  GnssVcpMpuStatus snapshot;
  uint8_t attempt;
  uint8_t coherent = 0u;
  uint32_t age_ticks;
  uint64_t age_limit_ticks;
  bool presentable = false;

  /* Default answer: invalid. Every rejection below returns this, so the caller
     can always encode what it is given. A NULL destination stays untouched. */
  if (status != NULL)
  {
    MPU_STATUS_SetInvalid(status);
  }

  if (status == NULL)
  {
    return false;
  }

  for (attempt = 0u; attempt < MPU_STATUS_READ_ATTEMPTS; attempt++)
  {
    if (GNSS_VCP_TryReadMpuStatus(GNSS_VCP_MPU_STATUS, &snapshot) != 0u)
    {
      coherent = 1u;
      break;
    }
  }

  if (coherent == 0u)
  {
    return false; /* CPU1 kept writing: report invalid rather than an old one */
  }

  if (snapshot.initialized != GNSS_VCP_MPU_STATUS_INIT_MARK)
  {
    /* Never initialised, or CPU1 restarted and has not set the region up yet.
       Forget the observation and wait for a new one. */
    mpu_snapshot_watch.observed = false;
    mpu_snapshot_watch.change_seen = false;
    return false;
  }

  /* The age limit is converted into this core's counter instead of converting
     the age into milliseconds, which would move the wrap point of the
     arithmetic. */
  age_limit_ticks = (((uint64_t)GNSS_VCP_MPU_MAX_DATA_AGE_MS) * MPU_STATUS_TICKS_PER_SECOND) / 1000u;

  if (mpu_snapshot_watch.observed == false)
  {
    /* First coherent publication since this core started: CPU1 may have
       published it arbitrarily long before this read, so nothing here bounds
       its age. Remember which publication it is and wait until a later read
       finds it replaced. */
    MPU_STATUS_RecordObservation(&mpu_snapshot_watch, snapshot.seq, HAL_GetTick(), false);
    return false;
  }

  /* Both values come from this core's counter, so the difference is valid and
     survives the counter's own wrap. */
  age_ticks = HAL_GetTick() - mpu_snapshot_watch.observed_ticks;

  if (mpu_snapshot_watch.sequence == snapshot.seq)
  {
    /* Same publication as the recorded one, so only its own record dates it:
       presentable when that record was made by a read that saw the
       replacement within one age window - otherwise its age is unbounded -
       and only until the window elapses, which expires a CPU1 that stopped
       publishing. The record is kept, so an expired publication stays expired
       until it is replaced. */
    presentable = (mpu_snapshot_watch.change_seen && ((uint64_t)age_ticks <= age_limit_ticks));
  }
  else if ((uint64_t)age_ticks <= age_limit_ticks)
  {
    /* A different publication appeared after the recorded one, hence it was
       published between the two reads and is no older than their distance. */
    MPU_STATUS_RecordObservation(&mpu_snapshot_watch, snapshot.seq, HAL_GetTick(), true);
    presentable = true;
  }
  else
  {
    /* The replacement is only noticed after a whole age window has passed, so
       the new publication may itself predate this read by almost as much.
       Record it and wait for the next replacement, exactly like the first
       observation. */
    MPU_STATUS_RecordObservation(&mpu_snapshot_watch, snapshot.seq, HAL_GetTick(), false);
    return false;
  }

  if ((presentable == false) || (snapshot.valid == 0u))
  {
    /* Either the publication cannot be dated, or CPU1 published "invalid". The
       publication is still recorded, so the next replacement is accepted
       without losing an interval. */
    return false;
  }

  status->seq = snapshot.seq;
  /* Diagnostic copy of the producer's stamp: it belongs to CPU1's clock and is
     never compared with now_ticks. */
  status->update_timestamp_ms = snapshot.update_timestamp_ms;
  status->valid = 1u;
  /* Heading validity is copied, never derived from `valid`: a device without a
     magnetometer publishes roll/pitch with heading_valid clear. */
  status->heading_valid = snapshot.heading_valid;
  status->model = snapshot.model;
  status->heading = snapshot.heading;
  status->roll = snapshot.roll;
  status->pitch = snapshot.pitch;

  return true;
}

/* USER CODE BEGIN 1 */

/**
  * @brief  Periodic freshness observation for the MPU snapshot watch.
  *         Reads the shared snapshot coherently and updates the watch state.
  *         Does not return a status; the result is cached in the watch for
  *         the next MPU_STATUS_ReadLatest call.
  * @note   Call from a periodic timer (~100 ms) in the CPU2 application context.
  */
void MPU_STATUS_ObserveFreshness(void)
{
  GnssVcpMpuStatus snapshot;
  uint8_t attempt;
  uint8_t coherent = 0u;
  uint32_t age_ticks;
  uint64_t age_limit_ticks;

  for (attempt = 0u; attempt < MPU_STATUS_READ_ATTEMPTS; attempt++)
  {
    if (GNSS_VCP_TryReadMpuStatus(GNSS_VCP_MPU_STATUS, &snapshot) != 0u)
    {
      coherent = 1u;
      break;
    }
  }

  if (coherent == 0u)
  {
    return; /* CPU1 kept writing: keep existing watch state */
  }

  if (snapshot.initialized != GNSS_VCP_MPU_STATUS_INIT_MARK)
  {
    /* Never initialised, or CPU1 restarted and has not set the region up yet.
       Forget the observation and wait for a new one. */
    mpu_snapshot_watch.observed = false;
    mpu_snapshot_watch.change_seen = false;
    return;
  }

  /* The age limit is converted into this core's counter. */
  age_limit_ticks = (((uint64_t)GNSS_VCP_MPU_MAX_DATA_AGE_MS) * MPU_STATUS_TICKS_PER_SECOND) / 1000u;

  if (mpu_snapshot_watch.observed == false)
  {
    /* First coherent publication since this core started: CPU1 may have
       published it arbitrarily long before this read, so nothing here bounds
       its age. Remember which publication it is and wait until a later read
       finds it replaced. */
    MPU_STATUS_RecordObservation(&mpu_snapshot_watch, snapshot.seq, HAL_GetTick(), false);
    return;
  }

  /* Both values come from this core's counter, so the difference is valid and
     survives the counter's own wrap. */
  age_ticks = HAL_GetTick() - mpu_snapshot_watch.observed_ticks;

  if (mpu_snapshot_watch.sequence == snapshot.seq)
  {
    /* Same publication as the recorded one. The watch state already captures
       whether we saw a replacement within one age window (change_seen).
       Nothing to update here; the age check is done at telemetry time. */
    return;
  }
  else if ((uint64_t)age_ticks <= age_limit_ticks)
  {
    /* A different publication appeared after the recorded one, hence it was
       published between the two reads and is no older than their distance.
       This replacement was seen within one age window, so observed_ticks
       bounds its age. */
    MPU_STATUS_RecordObservation(&mpu_snapshot_watch, snapshot.seq, HAL_GetTick(), true);
  }
  else
  {
    /* The replacement is only noticed after a whole age window has passed, so
       the new publication may itself predate this read by almost as much.
       Record it and wait for the next replacement, exactly like the first
       observation. */
    MPU_STATUS_RecordObservation(&mpu_snapshot_watch, snapshot.seq, HAL_GetTick(), false);
  }
}

/* USER CODE END 1 */

#else /* GNSS_VCP_BARRIER_HOST */
/* Host-testable versions with explicit clock parameters. */

bool MPU_STATUS_ReadLatest_Host(const GnssVcpMpuStatus *shared,
                                uint32_t now_ticks,
                                uint32_t ticks_per_second,
                                MpuSnapshotWatch *watch,
                                MpuTelemetryStatus *status)
{
  GnssVcpMpuStatus snapshot;
  uint8_t attempt;
  uint8_t coherent = 0u;
  uint32_t age_ticks;
  uint64_t age_limit_ticks;
  bool presentable = false;

  if (status != NULL)
  {
    MPU_STATUS_SetInvalid(status);
  }

  if ((shared == NULL) || (watch == NULL) || (status == NULL) ||
      (ticks_per_second == 0u))
  {
    return false;
  }

  for (attempt = 0u; attempt < MPU_STATUS_READ_ATTEMPTS; attempt++)
  {
    if (GNSS_VCP_TryReadMpuStatus(shared, &snapshot) != 0u)
    {
      coherent = 1u;
      break;
    }
  }

  if (coherent == 0u)
  {
    return false;
  }

  if (snapshot.initialized != GNSS_VCP_MPU_STATUS_INIT_MARK)
  {
    watch->observed = false;
    watch->change_seen = false;
    return false;
  }

  age_limit_ticks = (((uint64_t)GNSS_VCP_MPU_MAX_DATA_AGE_MS) * ticks_per_second) / 1000u;

  if (watch->observed == false)
  {
    MPU_STATUS_RecordObservation(watch, snapshot.seq, now_ticks, false);
    return false;
  }

  age_ticks = now_ticks - watch->observed_ticks;

  if (watch->sequence == snapshot.seq)
  {
    presentable = (watch->change_seen && ((uint64_t)age_ticks <= age_limit_ticks));
  }
  else if ((uint64_t)age_ticks <= age_limit_ticks)
  {
    MPU_STATUS_RecordObservation(watch, snapshot.seq, now_ticks, true);
    presentable = true;
  }
  else
  {
    MPU_STATUS_RecordObservation(watch, snapshot.seq, now_ticks, false);
    return false;
  }

  if ((presentable == false) || (snapshot.valid == 0u))
  {
    return false;
  }

  status->seq = snapshot.seq;
  status->update_timestamp_ms = snapshot.update_timestamp_ms;
  status->valid = 1u;
  status->heading_valid = snapshot.heading_valid;
  status->model = snapshot.model;
  status->heading = snapshot.heading;
  status->roll = snapshot.roll;
  status->pitch = snapshot.pitch;

  return true;
}

void MPU_STATUS_ObserveFreshness_Host(const GnssVcpMpuStatus *shared,
                                      uint32_t now_ticks,
                                      uint32_t ticks_per_second,
                                      MpuSnapshotWatch *watch)
{
  GnssVcpMpuStatus snapshot;
  uint8_t attempt;
  uint8_t coherent = 0u;
  uint32_t age_ticks;
  uint64_t age_limit_ticks;

  if ((shared == NULL) || (watch == NULL) || (ticks_per_second == 0u))
  {
    return;
  }

  for (attempt = 0u; attempt < MPU_STATUS_READ_ATTEMPTS; attempt++)
  {
    if (GNSS_VCP_TryReadMpuStatus(shared, &snapshot) != 0u)
    {
      coherent = 1u;
      break;
    }
  }

  if (coherent == 0u)
  {
    return;
  }

  if (snapshot.initialized != GNSS_VCP_MPU_STATUS_INIT_MARK)
  {
    watch->observed = false;
    watch->change_seen = false;
    return;
  }

  age_limit_ticks = (((uint64_t)GNSS_VCP_MPU_MAX_DATA_AGE_MS) * ticks_per_second) / 1000u;

  if (watch->observed == false)
  {
    MPU_STATUS_RecordObservation(watch, snapshot.seq, now_ticks, false);
    return;
  }

  age_ticks = now_ticks - watch->observed_ticks;

  if (watch->sequence == snapshot.seq)
  {
    return;
  }
  else if ((uint64_t)age_ticks <= age_limit_ticks)
  {
    MPU_STATUS_RecordObservation(watch, snapshot.seq, now_ticks, true);
  }
  else
  {
    MPU_STATUS_RecordObservation(watch, snapshot.seq, now_ticks, false);
  }
}

#endif /* GNSS_VCP_BARRIER_HOST */
