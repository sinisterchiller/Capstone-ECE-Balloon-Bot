/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    gnss_vcp.h
  * @brief   Publishes the decoded GPS lines of the NMEA sentences received on
  *          USART1, next to the raw sentences when the raw trace switch is on,
  *          for printing on the ST-LINK VCP by CPU2 (see gnss_vcp_mailbox.h for
  *          the transfer and nmea_gga.h for the decoding).
  ******************************************************************************
  */
/* USER CODE END Header */
/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __GNSS_VCP_H__
#define __GNSS_VCP_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include <stdint.h>

/* External variables --------------------------------------------------------*/
/**
  * @brief Lines that did not fit in the mailbox ring (CPU2 not draining, e.g.
  *        no CPU2 image flashed). Published for debugger inspection.
  */
extern volatile uint32_t gnss_vcp_lines_dropped;

/**
  * @brief GGA sentences rejected as malformed or checksum-failed. An error line
  *        follows each of them, next to the raw sentence when it was forwarded
  *        first. Published for debugger inspection.
  */
extern volatile uint32_t gnss_vcp_gga_invalid;

/* Exported functions prototypes ---------------------------------------------*/
/**
  * @brief  Resets the mailbox ring for the CPU2 consumer.
  * @note   Call once during initialisation, before HAL_PWREx_ReleaseCore():
  *         the ring lives in RAM that no startup file clears, so CPU2 would
  *         otherwise see random indices after power-up.
  */
void GNSS_VCP_Init(void);

/**
  * @brief  Queues the decoded line of the newest complete NMEA sentence, if any,
  *         preceded by the raw sentence when raw tracing is enabled.
  * @note   Call from the main loop only, never from the USART1 interrupt: the
  *         interrupt keeps assembling the next line. The call never waits and
  *         never blocks - it copies at most one bounded line and queues at most
  *         two bounded lines, or returns. The raw sentence is queued first so
  *         it wins when the ring cannot hold both.
  * @note   The decoded line is a position for a GGA fix, a no-fix status line
  *         carrying the GGA satellite count for fix quality 0, or an error line
  *         for an unusable sentence. Non-GGA sentences queue nothing unless raw
  *         tracing is enabled.
  */
void GNSS_VCP_Process(void);

/**
  * @brief  Checks and expires stale GPS fixes in the shared status snapshot.
  * @note   Call periodically from the main loop (e.g. every 1-5 seconds).
  *         If the current fix is older than TELEMETRY_MAX_FIX_AGE_MS,
  *         it is invalidated (fix_valid cleared).
  */
void GNSS_VCP_CheckFixExpiration(void);

#ifdef __cplusplus
}
#endif

#endif /* __GNSS_VCP_H__ */
