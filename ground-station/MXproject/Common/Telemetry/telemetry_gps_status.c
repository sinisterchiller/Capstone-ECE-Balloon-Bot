/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    telemetry_gps_status.c
  * @brief   Consumption side of the CPU1 GPS status snapshot (see
  *          telemetry_gps_status.h).
  *
  * The publication a read finds is never presented on that same read when this
  * core cannot date it: only a publication observed to replace an earlier one
  * was certainly published after a read of this core, and only such a
  * publication has an age the local counter bounds. This is what keeps a
  * snapshot inherited from an earlier CPU1 run - CPU1 halted long before CPU2
  * started - from travelling as a current fix.
  *
  * Pure C with no HAL dependency: the caller supplies the current value and
  * the rate of its own clock, so the same code runs on the target and in the
  * host test.
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "telemetry_gps_status.h"

#include <stddef.h>

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/**
  * @brief Attempts at a coherent seqlock read.
  * @note  CPU1 holds the status in the written state for a handful of stores
  *        and only when a new GGA sentence or an expiry lands, so a few
  *        immediate retries settle it. Giving up keeps the caller bounded: the
  *        radio task must never spin on the other core.
  */
#define TELEMETRY_STATUS_READ_ATTEMPTS 3u
/* USER CODE END PD */

/* Private functions ---------------------------------------------------------*/
/**
  * @brief  Fills a status as "no fix": no validity, no coordinates, no stamps.
  * @param  status  Destination status.
  */
static void Telemetry_SetNoFix(TelemetryGpsStatus *status)
{
  status->fix_sequence = 0u;
  status->fix_timestamp_ms = 0u;
  status->fix_valid = 0u;
  status->latitude_udeg = 0;
  status->longitude_udeg = 0;
  status->altitude_cm = 0;
  status->satellites_used = 0u;
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
static void Telemetry_RecordObservation(TelemetryGpsSnapshotWatch *watch,
                                        uint32_t sequence,
                                        uint32_t now_ticks,
                                        bool change_seen)
{
  watch->sequence = sequence;
  watch->observed_ticks = now_ticks;
  watch->observed = true;
  watch->change_seen = change_seen;
}

/* Exported functions --------------------------------------------------------*/
bool Telemetry_ReadGpsStatusSnapshot(const GnssVcpGpsStatus *shared,
                                     uint32_t now_ticks,
                                     uint32_t ticks_per_second,
                                     TelemetryGpsSnapshotWatch *watch,
                                     TelemetryGpsStatus *status)
{
  GnssVcpGpsStatus snapshot;
  uint8_t attempt;
  uint8_t coherent = 0u;
  uint32_t age_ticks;
  uint64_t age_limit_ticks;
  bool presentable = false;

  /* Default answer: no fix. Every rejection below returns this, so the caller
     can always encode what it is given. A NULL destination stays untouched. */
  if (status != NULL)
  {
    Telemetry_SetNoFix(status);
  }

  if ((shared == NULL) || (watch == NULL) || (status == NULL) ||
      (ticks_per_second == 0u))
  {
    return false;
  }

  for (attempt = 0u; attempt < TELEMETRY_STATUS_READ_ATTEMPTS; attempt++)
  {
    if (GNSS_VCP_TryReadGpsStatus(shared, &snapshot) != 0u)
    {
      coherent = 1u;
      break;
    }
  }

  if (coherent == 0u)
  {
    return false; /* CPU1 kept writing: report no fix rather than an old one */
  }

  if (snapshot.initialized != GNSS_VCP_GPS_STATUS_INIT_MARK)
  {
    /* Never initialised, or CPU1 restarted and has not set the region up yet
       (a reset does not clear the sequence, so the numbers of an earlier image
       must not be reused). Forget the observation and wait for a new one. */
    watch->observed = false;
    watch->change_seen = false;
    return false;
  }

  /* The age limit is converted into this core's counter instead of converting
     the age into milliseconds, which would move the wrap point of the
     arithmetic. */
  age_limit_ticks = (((uint64_t)TELEMETRY_MAX_FIX_AGE_MS) * ticks_per_second) / 1000u;

  if (watch->observed == false)
  {
    /* First coherent publication since this core started: CPU1 may have
       published it arbitrarily long before this read, so nothing here bounds
       its age. Remember which publication it is and wait until a later read
       finds it replaced. */
    Telemetry_RecordObservation(watch, snapshot.seq, now_ticks, false);
    return false;
  }

  /* Both values come from this core's counter, so the difference is valid and
     survives the counter's own wrap. */
  age_ticks = now_ticks - watch->observed_ticks;

  if (watch->sequence == snapshot.seq)
  {
    /* Same publication as the recorded one, so only its own record dates it:
       presentable when that record was made by a read that saw the
       replacement within one age window - otherwise its age is unbounded -
       and only until the window elapses, which expires a CPU1 that stopped
       publishing. The record is kept, so an expired publication stays expired
       until it is replaced. */
    presentable = (watch->change_seen && ((uint64_t)age_ticks <= age_limit_ticks));
  }
  else if ((uint64_t)age_ticks <= age_limit_ticks)
  {
    /* A different publication appeared after the recorded one, hence it was
       published between the two reads and is no older than their distance. */
    Telemetry_RecordObservation(watch, snapshot.seq, now_ticks, true);
    presentable = true;
  }
  else
  {
    /* The replacement is only noticed after a whole age window has passed, so
       the new publication may itself predate this read by almost as much.
       Record it and wait for the next replacement, exactly like the first
       observation. */
    Telemetry_RecordObservation(watch, snapshot.seq, now_ticks, false);
    return false;
  }

  if ((presentable == false) || (snapshot.fix_valid == 0u))
  {
    /* Either the publication cannot be dated, or CPU1 published "no fix". The
       publication is still recorded, so the next replacement is accepted
       without losing an interval. */
    return false;
  }

  status->fix_sequence = snapshot.seq;
  /* Diagnostic copy of the producer's stamp: it belongs to CPU1's clock and is
     never compared with now_ticks. */
  status->fix_timestamp_ms = snapshot.fix_timestamp_ms;
  status->fix_valid = 1u;
  status->latitude_udeg = snapshot.latitude_udeg;
  status->longitude_udeg = snapshot.longitude_udeg;
  status->altitude_cm = snapshot.altitude_cm;
  status->satellites_used = snapshot.satellites_used;

  return true;
}
