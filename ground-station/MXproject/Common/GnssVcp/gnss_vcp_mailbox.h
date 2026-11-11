/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    gnss_vcp_mailbox.h
  * @brief   Shared-memory byte ring carrying received NMEA sentences from CPU1
  *          (Cortex-M4) to CPU2 (Cortex-M0+), which owns the ST-LINK VCP trace.
  *          Also provides a coherent GPS status snapshot for telemetry.
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
  *
  * GPS Status Snapshot (single writer / single reader):
  *  - CPU1 updates the `gps_status` structure and then publishes a new
  *    `gps_status_seq` value after a DMB, so CPU2 never observes a partially
  *    written status.
  *  - CPU2 snapshots `gps_status_seq`, copies the status, then verifies the
  *    sequence has not changed. This gives a coherent view without locks.
  *  - `initialized` carries GNSS_VCP_GPS_STATUS_INIT_MARK once CPU1 has set the
  *    region up, so CPU2 can tell a never-initialised status (random RAM after
  *    power-up, or a CPU1 image without GNSS_VCP_Init()) from a published
  *    no-fix status.
  *  - A fix is stale if its age exceeds TELEMETRY_MAX_FIX_AGE_MS; CPU1 clears
  *    `fix_valid` in that case before publishing. CPU1's timestamps use CPU1's
  *    clock and CPU2's ticks use CPU2's clock, so the two are never subtracted
  *    from each other: the CPU2 consumer (Common/Telemetry/telemetry_gps_status.h)
  *    measures the age of the last publication it observed on its own clock,
  *    which also expires a snapshot that CPU1 stopped refreshing.
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

/* The publication protocol needs a data memory barrier on the target. The host
   test build has no CMSIS headers and runs on one thread, so it defines
   GNSS_VCP_BARRIER_HOST and the barrier compiles to nothing. */
#if defined(GNSS_VCP_BARRIER_HOST)
#define GNSS_VCP_BARRIER() ((void)0)
#else
#include "cmsis_compiler.h"
#define GNSS_VCP_BARRIER() __DMB()
#endif

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

/**
  * @brief Offset of the GPS status snapshot within the reserved region.
  *        Placed after the ring to keep the existing ring layout unchanged.
  */
#define GNSS_VCP_GPS_STATUS_OFFSET (sizeof(GnssVcpMailbox))

/**
  * @brief Value of GnssVcpGpsStatus::initialized once CPU1 has set the status
  *        region up. Any other value means "never initialised".
  */
#define GNSS_VCP_GPS_STATUS_INIT_MARK 0xA5u

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

/**
  * @brief GPS status snapshot for telemetry, updated by CPU1 and read by CPU2.
  * @note  All members are volatile for concurrent access. CPU1 writes the
  *        fields then publishes `seq` after a DMB; CPU2 reads `seq`, copies
  *        the fields, then re-reads `seq` to detect concurrent updates.
  */
typedef struct
{
  volatile uint32_t seq;                         /*!< publication sequence, even = stable */
  volatile uint32_t fix_timestamp_ms;            /*!< CPU1 HAL_GetTick() when fix was validated */
  volatile uint8_t  fix_valid;                   /*!< 1 = valid fix, 0 = no fix / stale */
  volatile uint8_t  _pad[3];                     /*!< alignment */
  volatile int32_t  latitude_udeg;               /*!< degrees * 1,000,000, signed */
  volatile int32_t  longitude_udeg;              /*!< degrees * 1,000,000, signed */
  volatile int32_t  altitude_cm;                 /*!< centimetres above MSL, signed */
  volatile uint8_t  satellites_used;             /*!< satellites in solution (GGA field 7) */
  volatile uint8_t  initialized;                 /*!< GNSS_VCP_GPS_STATUS_INIT_MARK once set up */
  volatile uint8_t  _reserved[2];                /*!< padding */
} GnssVcpGpsStatus;

/* Exported macro ------------------------------------------------------------*/
/**
  * @brief Access to the mailbox in shared SRAM1.
  */
#define GNSS_VCP_MAILBOX  ((GnssVcpMailbox *)GNSS_VCP_MAILBOX_BASE)

/**
  * @brief Access to the GPS status snapshot in shared SRAM1.
  */
#define GNSS_VCP_GPS_STATUS \
  ((GnssVcpGpsStatus *)((uint8_t *)GNSS_VCP_MAILBOX_BASE + GNSS_VCP_GPS_STATUS_OFFSET))

/* Exported inline functions -----------------------------------------------*/
/**
  * @brief  Atomically publishes a new GPS status from CPU1.
  * @param  status  Pointer to the shared GPS status structure.
  * @param  fix_timestamp_ms  CPU1 HAL_GetTick() value when the fix was validated (CPU1 clock).
  * @param  fix_valid  1 if fix is valid, 0 if no fix or stale.
  * @param  latitude_udeg  Latitude in microdegrees (valid only if fix_valid).
  * @param  longitude_udeg  Longitude in microdegrees (valid only if fix_valid).
  * @param  altitude_cm  Altitude in centimetres (valid only if fix_valid).
  * @param  satellites_used  Satellites used in solution (valid only if fix_valid).
  * @note   Uses seq as a seqlock: increments to odd (writing), writes fields,
  *         DMB, increments to even (stable). CPU2 reads even seq, copies,
  *         verifies seq unchanged. The initialisation marker is set inside the
  *         write region, so a published status is always recognisable as
  *         initialised.
  */
static inline void GNSS_VCP_PublishGpsStatus(GnssVcpGpsStatus *status,
                                             uint32_t fix_timestamp_ms,
                                             uint8_t fix_valid,
                                             int32_t latitude_udeg,
                                             int32_t longitude_udeg,
                                             int32_t altitude_cm,
                                             uint8_t satellites_used)
{
  /* Start write: make seq odd. */
  status->seq += 1u;
  GNSS_VCP_BARRIER();

  /* Write all fields. */
  status->fix_timestamp_ms = fix_timestamp_ms;
  status->fix_valid = fix_valid;
  status->latitude_udeg = latitude_udeg;
  status->longitude_udeg = longitude_udeg;
  status->altitude_cm = altitude_cm;
  status->satellites_used = satellites_used;
  status->initialized = GNSS_VCP_GPS_STATUS_INIT_MARK;

  /* End write: make seq even, after DMB so fields are visible. */
  GNSS_VCP_BARRIER();
  status->seq += 1u;
}

/**
  * @brief  Attempts to atomically read a consistent GPS status snapshot on CPU2.
  * @param  status  Pointer to the shared GPS status structure.
  * @param  out  Destination for the copied status (non-volatile).
  * @return 1 on success (coherent snapshot), 0 if a concurrent write occurred.
  * @note   Caller may retry on failure. The output `seq` field is the even
  *         sequence number observed, which can be used to detect updates. The
  *         initialisation marker is copied as well; a value other than
  *         GNSS_VCP_GPS_STATUS_INIT_MARK means CPU1 has not set the status up
  *         yet and no field of the snapshot may be used.
  */
static inline uint8_t GNSS_VCP_TryReadGpsStatus(const GnssVcpGpsStatus *status,
                                                GnssVcpGpsStatus *out)
{
  uint32_t seq1, seq2;

  /* Snapshot seq (must be even for a stable read). */
  seq1 = status->seq;
  if (seq1 & 1u)
  {
    return 0u; /* write in progress */
  }
  GNSS_VCP_BARRIER();

  /* Copy fields. */
  out->fix_timestamp_ms = status->fix_timestamp_ms;
  out->fix_valid = status->fix_valid;
  out->latitude_udeg = status->latitude_udeg;
  out->longitude_udeg = status->longitude_udeg;
  out->altitude_cm = status->altitude_cm;
  out->satellites_used = status->satellites_used;
  out->initialized = status->initialized;

  /* Verify seq unchanged. */
  GNSS_VCP_BARRIER();
  seq2 = status->seq;
  if (seq1 != seq2)
  {
    return 0u; /* concurrent write, retry */
  }

  out->seq = seq1;
  return 1u;
}

#ifdef __cplusplus
}
#endif

#endif /* __GNSS_VCP_MAILBOX_H__ */
