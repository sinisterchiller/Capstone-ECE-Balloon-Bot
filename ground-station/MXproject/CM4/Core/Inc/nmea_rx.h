/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    nmea_rx.h
  * @brief   Bounded, interrupt-driven reception of NMEA 0183 lines on USART1.
  ******************************************************************************
  */
/* USER CODE END Header */
/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __NMEA_RX_H__
#define __NMEA_RX_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include <stdint.h>

/**
  * @brief Size of one sentence buffer, including the terminating NUL.
  * @note  NMEA 0183 caps a sentence at 82 characters including "$", checksum
  *        and CR/LF; 100 leaves headroom. This bound is what keeps a noisy or
  *        missing GPS line from overflowing RAM.
  */
#define NMEA_RX_SENTENCE_CAPACITY 100u

/**
  * @brief Reception state, published for debugger inspection.
  *
  * `sentence` is double buffered: the USART1 interrupt always assembles into
  * the half that `active` does *not* select and only flips `active` once the
  * terminating LF has been stored. The published half is therefore never
  * written while it is published.
  *
  * Publication order is text, then `active`, then `sequence`, each step
  * separated by a barrier: `sequence` is the last thing to change, so a reader
  * that observes a new `sequence` also observes the half that line lives in.
  * A reader must still take `active` and `sequence` as one snapshot and
  * re-check both after copying, because the interrupt can publish again while
  * the copy runs and turn the half it is reading into the assembly half.
  */
typedef struct
{
  volatile char sentence[2][NMEA_RX_SENTENCE_CAPACITY]; /*!< [active] = latest complete line */
  volatile uint32_t active;                             /*!< index of the published line, 0 or 1 */
  volatile uint32_t sequence;                           /*!< completed lines published so far */
  volatile uint32_t bytes_received;                     /*!< USART1 bytes taken by the receiver ISR */
  volatile uint32_t line_overflows;                     /*!< lines dropped for exceeding the capacity */
  volatile uint32_t uart_errors;                        /*!< HAL UART error callbacks (overrun/frame/noise) */
} NmeaRxState;

extern NmeaRxState nmea_rx;

/**
  * @brief  Arms the continuous one-byte interrupt reception on USART1.
  * @note   Call once after MX_USART1_UART_Init(); reception re-arms itself from
  *         the HAL callbacks afterwards.
  */
void NMEA_RX_Start(void);

#ifdef __cplusplus
}
#endif

#endif /* __NMEA_RX_H__ */
