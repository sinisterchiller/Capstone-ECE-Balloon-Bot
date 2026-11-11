/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    gnss_status.c
  * @brief   Hands the shared GPS status to the CPU2 telemetry application,
  *          together with the CPU2 time base the freshness check needs.
  *
  * The snapshot protocol and the no-fix rules live in
  * Common/Telemetry/telemetry_gps_status.c, which both the target and the host
  * test build; this file only binds it to this core's shared-memory address and
  * to the tick counter of this core.
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "gnss_status.h"
#include "gnss_vcp_mailbox.h"
#include "main.h"
#include "telemetry_gps_status.h"

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
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
#define GNSS_STATUS_TICKS_PER_SECOND ((uint32_t)1u << RTC_N_PREDIV_S)
/* USER CODE END PD */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */
/**
  * @brief Publication of the CPU1 GPS status last observed by this core.
  * @note  Zeroed, i.e. "nothing observed yet", until the first read. It makes
  *        the fix age a difference of two values of this core's counter, so it
  *        must not be reset while the application runs.
  */
static TelemetryGpsSnapshotWatch gps_snapshot_watch;
/* USER CODE END PV */

/* USER CODE BEGIN EF */

/* USER CODE END EF */

/* Exported functions --------------------------------------------------------*/
bool GNSS_STATUS_ReadLatest(TelemetryGpsStatus *status)
{
  return Telemetry_ReadGpsStatusSnapshot(GNSS_VCP_GPS_STATUS, HAL_GetTick(),
                                         GNSS_STATUS_TICKS_PER_SECOND,
                                         &gps_snapshot_watch, status);
}
