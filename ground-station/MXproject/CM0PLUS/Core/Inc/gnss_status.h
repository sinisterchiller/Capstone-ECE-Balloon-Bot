/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    gnss_status.h
  * @brief   CPU2 access to the GPS status published by CPU1, for telemetry.
  *
  * CPU2 does not receive NMEA itself: CPU1 decodes the GGA sentences and
  * publishes the latest fix in the shared snapshot of gnss_vcp_mailbox.h. This
  * module is the CPU2 entry point for that snapshot; it supplies the CPU2 time
  * base to the freshness check, so a fix that has expired by the time a packet
  * is built is reported as no fix.
  *
  * The time base is CPU2's own: HAL_GetTick() on this core is the RTC counter
  * at 1024 ticks per second (see sys_app.c), not CPU1's SysTick milliseconds,
  * so its value is passed to the check together with that rate and is never
  * compared with a CPU1 timestamp. The age of the fix is the CPU2 time elapsed
  * since the publication that carried it was first observed, which also
  * expires a snapshot that CPU1 stopped refreshing. A publication this core
  * has not yet seen replaced is not dated at all and is reported as no fix, so
  * the packet of the first telemetry interval after a CPU2 (re)start carries no
  * fix even though CPU1 may have left a valid one behind.
  ******************************************************************************
  */
/* USER CODE END Header */
/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __GNSS_STATUS_H__
#define __GNSS_STATUS_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include <stdbool.h>

#include "telemetry.h"

/* Exported functions prototypes ---------------------------------------------*/
/**
  * @brief  Reads the latest GPS status for telemetry, with a freshness check.
  * @param  status  Receives the status to transmit. Always filled.
  * @return true when `status` carries a current valid fix, false when the
  *         balloon must be reported as having no fix (uninitialised snapshot,
  *         snapshot busy, published no-fix, a publication not yet seen
  *         replaced, or a publication older than TELEMETRY_MAX_FIX_AGE_MS on
  *         CPU2's clock).
  * @note   Call from the CPU2 application task, never from an interrupt: it
  *         reads the shared snapshot once and never waits for CPU1. Stage 3
  *         uses this as the single GPS input of the telemetry packet.
  * @note   Keeps the observation state of the last publication it saw, so the
  *         calls must come from one context and in occurrence order.
  * @note   "Expired" is measured with this core's tick counter (1024 ticks per
  *         second, wrapping after about 48 days), not with CPU1's stamps.
  */
bool GNSS_STATUS_ReadLatest(TelemetryGpsStatus *status);

#ifdef __cplusplus
}
#endif

#endif /* __GNSS_STATUS_H__ */
