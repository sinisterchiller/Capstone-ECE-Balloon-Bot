/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    gnss_vcp_trace.h
  * @brief   Prints the NMEA lines received by CPU1 on the ST-LINK VCP.
  ******************************************************************************
  */
/* USER CODE END Header */
/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __GNSS_VCP_TRACE_H__
#define __GNSS_VCP_TRACE_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include <stdint.h>

/* External variables --------------------------------------------------------*/
/**
  * @brief Lines lost because the trace FIFO was full when they were printed.
  *        Published for debugger inspection.
  */
extern volatile uint32_t gnss_vcp_trace_dropped;

/* Exported functions prototypes ---------------------------------------------*/
/**
  * @brief  Registers the drain task and its polling timer.
  * @note   Call once after MX_SubGHz_Phy_Init() (UTIL_ADV_TRACE_Init() and
  *         UTIL_TIMER_Init() must already have run). Lines are only printed
  *         from the sequencer task, never from the timer interrupt.
  */
void GNSS_VCP_TraceInit(void);

#ifdef __cplusplus
}
#endif

#endif /* __GNSS_VCP_TRACE_H__ */
