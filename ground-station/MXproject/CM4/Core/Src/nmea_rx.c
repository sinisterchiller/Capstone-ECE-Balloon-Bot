/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    nmea_rx.c
  * @brief   Bounded, interrupt-driven reception of NMEA 0183 lines on USART1.
  *
  * Lines are only assembled, never parsed: everything between the start of a
  * line and the terminating LF is stored verbatim (CR of the CR/LF pair is
  * dropped), and the latest complete line is handed out through `nmea_rx`.
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "nmea_rx.h"
#include "usart.h"

NmeaRxState nmea_rx;

/* Byte handed back to HAL_UART_Receive_IT; written by the USART1 ISR only. */
static uint8_t rx_byte;

/* Assembly cursor inside the working half, sentence[nmea_rx.active ^ 1]. */
static uint16_t assemble_len;

/* Set while the tail of an unusable line is being discarded, up to the next LF. */
static uint8_t dropping_line;

/**
  * @brief  Re-arms the one-byte interrupt reception.
  * @note   HAL_BUSY means a reception is already armed: a recoverable framing or
  *         noise error completes the transfer and re-arms it before the error
  *         callback runs, so there is nothing to do in that case.
  */
static void NMEA_RX_Arm(void)
{
  (void)HAL_UART_Receive_IT(&huart1, &rx_byte, 1);
}

/**
  * @brief  Abandons the line under assembly and swallows its tail up to the next LF.
  */
static void NMEA_RX_DropLine(void)
{
  assemble_len = 0;
  dropping_line = 1;
}

/**
  * @brief  Appends one received byte to the line under assembly.
  * @param  byte received byte
  */
static void NMEA_RX_AppendByte(uint8_t byte)
{
  char *line = (char *)nmea_rx.sentence[nmea_rx.active ^ 1u];

  nmea_rx.bytes_received++;

  if (dropping_line)
  {
    if (byte == (uint8_t)'\n')
    {
      dropping_line = 0; /* resynchronised: the next line starts here */
    }
    return;
  }

  if (byte == (uint8_t)'\n')
  {
    if (assemble_len > 0u)
    {
      line[assemble_len] = '\0';
      __DMB(); /* the sentence text and its NUL are in the half before the switch */
      nmea_rx.active ^= 1u; /* publish: this half becomes the readable one (the
                               previous line stays readable until this point) */
      __DMB(); /* the new half before the new sequence number, so a reader that
                  observes a new sequence also observes the half it names */
      nmea_rx.sequence++;
    }
    assemble_len = 0;
    return;
  }

  if (byte == (uint8_t)'\r')
  {
    return; /* terminator half, not part of the sentence text */
  }

  if (assemble_len >= (NMEA_RX_SENTENCE_CAPACITY - 1u))
  {
    /* Line longer than the buffer: drop it without writing past the bound. */
    nmea_rx.line_overflows++;
    NMEA_RX_DropLine();
    return;
  }

  line[assemble_len] = (char)byte;
  assemble_len++;
}

void NMEA_RX_Start(void)
{
  assemble_len = 0;
  dropping_line = 0;
  NMEA_RX_Arm();
}

/**
  * @brief  One-byte reception complete: store the byte and re-arm.
  */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART1)
  {
    NMEA_RX_AppendByte(rx_byte);
    NMEA_RX_Arm();
  }
}

/**
  * @brief  UART error (overrun, framing, noise).
  * @note   An overrun aborts the interrupt reception in the HAL, so the receiver
  *         has to be re-armed or it would stall for the rest of the flight. The
  *         partial line is dropped: bytes are known to be missing or corrupt.
  */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART1)
  {
    nmea_rx.uart_errors++;
    NMEA_RX_DropLine();
    NMEA_RX_Arm();
  }
}
