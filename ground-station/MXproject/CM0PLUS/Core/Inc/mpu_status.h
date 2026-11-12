/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    mpu_status.h
  * @brief   CPU2 access to the MPU orientation status published by CPU1, for
  *          telemetry.
  *
  * CPU2 does not communicate with the MPU: CPU1 polls the sensor over I2C and
  * publishes the latest reading in the shared snapshot of
  * gnss_vcp_mailbox.h. This module is the CPU2 entry point for that snapshot;
  * it supplies the CPU2 time base to the freshness check, so a reading that
  * has expired by the time a packet is built is reported as invalid.
  *
  * The time base is CPU2's own: HAL_GetTick() on this core is the RTC counter
  * at 1024 ticks per second (see sys_app.c), not CPU1's SysTick milliseconds,
  * so its value is passed to the check together with that rate and is never
  * compared with a CPU1 timestamp. The age of the reading is the CPU2 time
  * elapsed since the publication that carried it was first observed, which
  * also expires a snapshot that CPU1 stopped refreshing. A publication this
  * core has not yet seen replaced is not dated at all and is reported as
  * invalid, so the packet of the first telemetry interval after a CPU2
  * (re)start carries no orientation data even though CPU1 may have left a
  * valid one behind.
  *
  * Heading validity is carried through unchanged: an MPU-6500 (or an MPU-9250
  * whose magnetometer is not usable) publishes valid roll/pitch with
  * heading_valid clear and heading zero. The consumer never derives heading
  * availability from the orientation validity flag.
  ******************************************************************************
  */
/* USER CODE END Header */
/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MPU_STATUS_H__
#define __MPU_STATUS_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include <stdbool.h>
#include <stdint.h>

#include "gnss_vcp_mailbox.h"

/* Exported types ------------------------------------------------------------*/
/**
 * @brief Non-volatile copy of the MPU orientation status for telemetry
 *        encoding. Mirrors the volatile shared-memory structure but without
 *        volatile.
 */
typedef struct
{
  uint32_t seq;                 /*!< publication sequence (even = stable) */
  uint32_t update_timestamp_ms; /*!< CPU1 HAL_GetTick() when data was read */
  uint8_t  valid;               /*!< 1 = orientation (roll/pitch) valid */
  uint8_t  heading_valid;       /*!< 1 = heading valid, 0 = unavailable / zero */
  uint8_t  model;               /*!< GNSS_VCP_MPU_MODEL_* device identifier */
  int16_t  heading;             /*!< heading, degrees * 16, 0 when heading_valid is 0 */
  int16_t  roll;                /*!< roll, degrees * 16 (-180..+180*16) */
  int16_t  pitch;               /*!< pitch, degrees * 16 (-90..+90*16) */
} MpuTelemetryStatus;

/**
  * @brief Consumer-side record of the last MPU status publication observed.
  *
  * The reading age is the time elapsed on the reading core's counter since the
  * publication that carried the reading was first observed, so the tracker
  * holds that publication and the local counter value of its first observation.
  * `change_seen` records whether that publication was observed to replace an
  * earlier one within one age window, i.e. whether the counter value it holds
  * actually bounds its age: only then may it be presented, and only until the
  * window has elapsed. It must be zero-initialised before the first read (all
  * zero means "nothing observed yet", which is also the state after a CPU2
  * reset) and then keep its value between reads of the same core.
  *
  * @note Do not copy the tracker between cores or reset it while the
  *       application runs: the recorded counter belongs to one clock.
  */
typedef struct
{
  uint32_t sequence;        /*!< seqlock sequence of the observed publication */
  uint32_t observed_ticks;  /*!< reading core's counter when it was first observed */
  bool     observed;        /*!< false until a publication has been recorded */
  bool     change_seen;     /*!< publication was seen to replace an earlier one
                                 within one age window, so observed_ticks bounds
                                 its age */
} MpuSnapshotWatch;

/* Exported functions prototypes ---------------------------------------------*/
/**
  * @brief  Reads the latest MPU orientation status for telemetry, with a
  *         freshness check.
  * @param  status  Receives the status to transmit. Always filled.
  * @return true when `status` carries a current valid reading, false when the
  *         balloon must be reported as having no valid MPU orientation data
  *         (uninitialised snapshot, snapshot busy, published invalid, a
  *         publication not yet seen replaced, or a publication older than
  *         GNSS_VCP_MPU_MAX_DATA_AGE_MS on CPU2's clock).
  * @note   Call from the CPU2 application task, never from an interrupt: it
  *         reads the shared snapshot once and never waits for CPU1.
  * @note   Keeps the observation state of the last publication it saw, so the
  *         calls must come from one context and in occurrence order.
  * @note   "Expired" is measured with this core's tick counter (1024 ticks per
  *         second, wrapping after about 48 days), not with CPU1's stamps.
  */
bool MPU_STATUS_ReadLatest(MpuTelemetryStatus *status);

/**
  * @brief  Periodic freshness observation for the MPU snapshot watch.
  *         Call from a periodic timer (~100 ms) to observe the latest
  *         publication and update the snapshot watch state. This allows the
  *         watch to see publications replaced within the 500 ms age window and
  *         establish change_seen=true, so that the 10-second telemetry read can
  *         present current data.
  * @note   Call from the CPU2 application context (timer callback), never from
  *         an interrupt. It reads the shared snapshot once and updates the
  *         watch; the result is cached for the next MPU_STATUS_ReadLatest call.
  */
void MPU_STATUS_ObserveFreshness(void);

#if defined(GNSS_VCP_BARRIER_HOST)
/* Host-testable versions with explicit clock parameters and an ordinary shared
   object instead of the fixed SRAM1 address. Declared only for the host build;
   the target build never sees them. */

bool MPU_STATUS_ReadLatest_Host(const GnssVcpMpuStatus *shared,
                                uint32_t now_ticks,
                                uint32_t ticks_per_second,
                                MpuSnapshotWatch *watch,
                                MpuTelemetryStatus *status);

void MPU_STATUS_ObserveFreshness_Host(const GnssVcpMpuStatus *shared,
                                      uint32_t now_ticks,
                                      uint32_t ticks_per_second,
                                      MpuSnapshotWatch *watch);
#endif /* GNSS_VCP_BARRIER_HOST */

#ifdef __cplusplus
}
#endif

#endif /* __MPU_STATUS_H__ */
