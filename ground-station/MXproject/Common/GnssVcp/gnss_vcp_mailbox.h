/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    gnss_vcp_mailbox.h
  * @brief   Shared-memory byte ring carrying received NMEA sentences from CPU1
  *          (Cortex-M4) to CPU2 (Cortex-M0+), which owns the ST-LINK VCP trace.
  *
  * CPU1 receives NMEA on USART1 (PB7, 9600 8N1) and must not drive USART2
  * (PA2/PA3, the ST-LINK VCP): CPU2 initialises it for the SubGHz_Phy trace and
  * transmits on it through DMA. The two cores therefore exchange the sentences
  * through this ring instead of sharing the UART. CPU1 is the only writer of
  * `write` and the ring bytes it publishes, CPU2 the only writer of `read`;
  * neither core ever writes memory the other publishes, so no lock is needed.
  *
  * Memory: the ring lives at the top of SRAM1, in 1 KB that
  * CM4/STM32WL55XX_FLASH_CM4.ld carves out of CPU1's RAM1 (RAM1 is 15 KB, not
  * 16 KB, and the reset stack pointer is the mailbox base). SRAM1 is the one
  * memory region both images already use - CPU1 for its .data/.bss/stack,
  * CPU2 for its own RAM1 at 0x20004000 - so it is the shared memory of this
  * device. SRAM2b is deliberately not used: it is CPU2-exclusive.
  *
  * Protocol (single producer / single consumer):
  *  - CPU1 appends whole lines ("$...*hh" followed by CR LF) and only then
  *    publishes the new `write` value, after a DMB, so CPU2 never observes a
  *    partially written line.
  *  - CPU2 copies bytes out of [read, write), prints them, and then publishes
  *    `read`. Publishing a stale `read` would only make CPU1 see less free
  *    space, so a late update can never corrupt the ring.
  *  - One byte of the ring is always kept free, so write == read unambiguously
  *    means empty. A line that does not fit is dropped by CPU1, never split.
  ******************************************************************************
  */
/* USER CODE END Header */
/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __GNSS_VCP_MAILBOX_H__
#define __GNSS_VCP_MAILBOX_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include <stdint.h>

/* Exported constants --------------------------------------------------------*/
/**
  * @brief Base address of the mailbox, matching the RAM1 size in
  *        CM4/STM32WL55XX_FLASH_CM4.ld (0x20000000 + 15 KB). CPU2 must not
  *        place anything here either: its RAM1 starts at 0x20004000.
  */
#define GNSS_VCP_MAILBOX_BASE      0x20003C00u

/**
  * @brief Size of the region reserved by the CPU1 linker script for this
  *        mailbox. Only the structure below is used; the rest is headroom.
  */
#define GNSS_VCP_MAILBOX_RESERVED  0x400u

/**
  * @brief Ring size in bytes. Power of two: indices are masked, not divided.
  */
#define GNSS_VCP_RING_SIZE         512u

/**
  * @brief Longest line the ring carries, hole for the NUL of the reader side
  *        included. An NMEA 0183 sentence is at most 82 characters including
  *        "$", checksum and CR LF, so this leaves headroom for receivers that
  *        emit longer proprietary sentences.
  */
#define GNSS_VCP_LINE_CAPACITY     104u

/* Exported types ------------------------------------------------------------*/
/**
  * @brief CPU1 to CPU2 sentence ring.
  * @note  All members are volatile: both cores access them concurrently and
  *        neither side may cache a value across the other's updates.
  */
typedef struct
{
  volatile uint32_t write;                       /*!< bytes appended by CPU1, monotonic */
  volatile uint32_t read;                        /*!< bytes consumed by CPU2, monotonic */
  volatile uint8_t  ring[GNSS_VCP_RING_SIZE];    /*!< byte stream, indexed module RING_SIZE */
} GnssVcpMailbox;

/* Exported macro ------------------------------------------------------------*/
/**
  * @brief Access to the mailbox in shared SRAM1.
  */
#define GNSS_VCP_MAILBOX  ((GnssVcpMailbox *)GNSS_VCP_MAILBOX_BASE)

#ifdef __cplusplus
}
#endif

#endif /* __GNSS_VCP_MAILBOX_H__ */
