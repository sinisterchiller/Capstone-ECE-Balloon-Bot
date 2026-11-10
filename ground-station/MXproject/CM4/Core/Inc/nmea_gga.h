/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    nmea_gga.h
  * @brief   Turns one NMEA GGA sentence into a human-readable GPS trace line.
  *
  * The line produced here lets the position be read without decoding NMEA by
  * hand: gnss_vcp.c prints it on the ST-LINK VCP, next to the raw sentence when
  * its raw trace switch is on. Only GGA carries a position fix, so GSV
  * satellite lists and the other sentence types are ignored.
  *
  * Two of the rendered lines are diagnostics rather than positions: a
  * checksum-valid GGA with fix quality 0 prints its GGA satellite count as a
  * status, which is what a receiver reports while it is still acquiring
  * satellites, and an unusable sentence prints a distinct error line.
  ******************************************************************************
  */
/* USER CODE END Header */
/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __NMEA_GGA_H__
#define __NMEA_GGA_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include <stdint.h>

/* Exported constants --------------------------------------------------------*/
/**
  * @brief Longest rendered line, including the terminating NUL.
  * @note  The worst case is the no-fix status line with the satellite count
  *        unavailable (91 bytes); the fix line is about 62 bytes. The value
  *        matches GNSS_VCP_LINE_CAPACITY, so one rendered line always fits one
  *        mailbox line while the parser still has a fixed bound to respect.
  */
#define NMEA_GGA_LINE_CAPACITY 104u

/* Exported types ------------------------------------------------------------*/
/**
  * @brief Outcome of rendering one sentence.
  */
typedef enum
{
  NMEA_GGA_NOT_GGA = 0, /*!< not a GP/GN GGA sentence: ignored */
  NMEA_GGA_NO_FIX,      /*!< GGA with fix quality 0: no-fix status line */
  NMEA_GGA_INVALID,     /*!< unusable GGA: error line, or no line at all when
                             the destination buffer cannot be written to */
  NMEA_GGA_FIX          /*!< position rendered */
} NmeaGgaStatus;

/* Exported functions prototypes ---------------------------------------------*/
/**
  * @brief  Renders the trace line for one complete, NUL-terminated sentence.
  * @param  sentence sentence text as assembled by nmea_rx.c, without CR/LF
  *                  (a trailing CR/LF is tolerated)
  * @param  destination buffer receiving the line, NUL-terminated
  * @param  capacity size of destination in bytes
  * @param  length receives the line length excluding the NUL, 0 when no line
  * @return NMEA_GGA_FIX for a rendered position, NMEA_GGA_NO_FIX for the no-fix
  *         status line, NMEA_GGA_INVALID for a malformed or checksum-failed GGA
  *         (an error line is produced), NMEA_GGA_NOT_GGA for any other
  *         sentence.
  * @note   The checksum is verified before any field is interpreted, so a
  *         corrupted sentence can never produce plausible coordinates. A GGA
  *         that claims a fix (quality > 0) but has an empty or out-of-range
  *         latitude, longitude or altitude, and a GGA whose fields cannot be
  *         decoded at all, produce the same error line instead of a position,
  *         so no stale or invented coordinate is ever printed.
  * @note   A GGA with fix quality 0 prints "GPS STATUS: No position fix yet
  *         (<n> satellites used). Check antenna and sky view.", where <n> is GGA
  *         field 7, the satellites used in the solution. That is not the GSV
  *         satellite count in view, and the status wording keeps a receiver
  *         that is still acquiring satellites from looking like a fault. When
  *         field 7 is missing, malformed or greater than 99, the line says
  *         "satellite count unavailable" rather than claiming zero.
  * @note   Degrees are rendered with integer arithmetic only (six fractional
  *         decimal places); the altitude is copied with the precision of its
  *         input field, so no floating-point printf support is needed.
  */
NmeaGgaStatus NMEA_GGA_RenderLine(const char *sentence, char *destination, uint16_t capacity,
                                  uint16_t *length);

#ifdef __cplusplus
}
#endif

#endif /* __NMEA_GGA_H__ */
