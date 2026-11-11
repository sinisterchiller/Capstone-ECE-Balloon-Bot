/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    gnss_vcp.c
  * @brief   Hands the assembled NMEA lines over to CPU2 for printing on the
  *          ST-LINK VCP.
  *
  * The USART1 interrupt (nmea_rx.c) only assembles lines; this module copies
  * the latest complete line into the shared ring from the main loop. Nothing is
  * transmitted from CPU1: CPU2 owns USART2 (PA2/PA3) and its DMA channel, so
  * the trace and the receiver buffers keep working exactly as before.
  *
  * A GGA fix is printed as human-readable decimal degrees and altitude
  * (nmea_gga.c), a GGA without a position prints a status line with its
  * satellite count, and an unusable GGA prints an error line. The raw $GP/$GN
  * sentences themselves are printed only when gnss_vcp_debug is true; with it
  * false the trace carries the decoded GPS lines alone and every other NMEA
  * sentence stays silent. When both lines are queued, the raw one goes first,
  * so a ring that lacks room for both keeps the debugging line.
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "gnss_vcp.h"
#include "gnss_vcp_mailbox.h"
#include "nmea_gga.h"
#include "nmea_rx.h"
#include "telemetry.h"

#include <stdbool.h>

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* A rendered GGA line is queued as one mailbox line, so it must fit the line
   bound the ring consumer reads. */
_Static_assert(NMEA_GGA_LINE_CAPACITY <= GNSS_VCP_LINE_CAPACITY,
               "GGA line does not fit the GNSS VCP mailbox line capacity");
/* USER CODE END PD */

/* Private variables ---------------------------------------------------------*/
volatile uint32_t gnss_vcp_lines_dropped;
volatile uint32_t gnss_vcp_gga_invalid;

/* USER CODE BEGIN PV */
/* Raw trace switch: true prints each received NMEA sentence as well, false
   prints only the human-readable GPS lines decoded from a GGA. */
static const bool gnss_vcp_debug = true;
/* Last nmea_rx.sequence value handled. */
static uint32_t published_sequence;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */
static uint16_t GNSS_VCP_CopyLine(char *destination, uint32_t active);
static uint8_t GNSS_VCP_Post(const char *line, uint16_t length);
static void GNSS_VCP_PostGgaLine(const char *line);
static void GNSS_VCP_UpdateGpsStatus(const char *line);
/* USER CODE END PFP */

/* Exported functions --------------------------------------------------------*/
void GNSS_VCP_Init(void)
{
  GnssVcpMailbox *mailbox = GNSS_VCP_MAILBOX;
  GnssVcpGpsStatus *gps_status = GNSS_VCP_GPS_STATUS;

  mailbox->write = 0u;
  mailbox->read = 0u;
  published_sequence = 0u;
  gnss_vcp_lines_dropped = 0u;
  gnss_vcp_gga_invalid = 0u;

  /* Initialise GPS status to no-fix. */
  gps_status->seq = 0u;
  gps_status->fix_timestamp_ms = 0u;
  gps_status->fix_valid = 0u;
  gps_status->latitude_udeg = 0;
  gps_status->longitude_udeg = 0;
  gps_status->altitude_cm = 0;
  gps_status->satellites_used = 0u;

  __DMB(); /* empty ring and initial GPS status visible to CPU2 */

  /* Mark the status as set up only after the no-fix fields are visible, so
     CPU2 never accepts the random RAM contents of an uninitialised status. */
  gps_status->initialized = GNSS_VCP_GPS_STATUS_INIT_MARK;
}

void GNSS_VCP_Process(void)
{
  char line[GNSS_VCP_LINE_CAPACITY];
  uint32_t active = nmea_rx.active; /* snapshot: the half and the number of the
                                       line it holds are taken together */
  uint32_t sequence = nmea_rx.sequence;
  uint16_t length;

  if (sequence == published_sequence)
  {
    return; /* no complete line since the last call */
  }

  length = GNSS_VCP_CopyLine(line, active);

  __DMB(); /* reader fence: the checks below must not move above the copy */

  if ((nmea_rx.active != active) || (nmea_rx.sequence != sequence))
  {
    /* The receiver published again while the snapshot was being taken or
       copied: the half read here may have become the assembly half, so the
       copy can be torn, and the sequence no longer names the line it holds.
       Post nothing; the next pass snapshots the pair again. */
    return;
  }

  published_sequence = sequence;

  /* Parse GGA for structured GPS status (telemetry). */
  GNSS_VCP_UpdateGpsStatus(line);

  if (gnss_vcp_debug)
  {
    if (GNSS_VCP_Post(line, length) == 0u)
    {
      /* The ring is out of room: the raw sentence has priority over the
         formatted one, so only it is dropped and counted here. */
      return;
    }
  }

  GNSS_VCP_PostGgaLine(line);
}

/* USER CODE BEGIN EF */

/* USER CODE END EF */

/* Private functions ---------------------------------------------------------*/
/**
  * @brief  Copies one published NMEA half out of the receiver state.
  * @param  destination buffer of at least GNSS_VCP_LINE_CAPACITY bytes
  * @param  active half to copy, taken from the caller's snapshot
  * @return line length, excluding the terminating NUL
  * @note   The bound is the receiver's half, not the destination: reading past
  *         the NMEA_RX_SENTENCE_CAPACITY bytes of the row selected by `active`
  *         would leave the object the pointer was taken from, and an in-row
  *         bound is also what keeps an optimising compiler from dropping the
  *         check. A line never needs more: the receiver terminates every
  *         published half inside its row.
  * @note   The caller validates the snapshot afterwards: the USART1 interrupt
  *         can publish again in the middle of this loop, which both flips
  *         `active` and turns the half read here into its assembly buffer.
  *         Copying a fixed, snapshot-selected half keeps the bytes and the
  *         line number consistent, and the validation drops the copy if the
  *         receiver moved on.
  */
static uint16_t GNSS_VCP_CopyLine(char *destination, uint32_t active)
{
  uint16_t length = 0u;

  while ((length < NMEA_RX_SENTENCE_CAPACITY) && (nmea_rx.sentence[active][length] != '\0'))
  {
    destination[length] = nmea_rx.sentence[active][length];
    length++;
  }
  destination[length] = '\0';
  return length;
}

/**
  * @brief  Appends one line to the mailbox ring, CR LF terminated.
  * @param  line sentence text without its own line terminator
  * @param  length sentence length
  * @return 1 when the whole line was queued, 0 when it was dropped
  * @note   Drops the line when it does not fit, so a consumer that stops
  *         draining (no CPU2 image) cannot wedge the reader with a torn line.
  */
static uint8_t GNSS_VCP_Post(const char *line, uint16_t length)
{
  GnssVcpMailbox *mailbox = GNSS_VCP_MAILBOX;
  uint32_t write = mailbox->write;
  uint32_t used = write - mailbox->read;
  uint32_t total = (uint32_t)length + 2u; /* sentence + CR LF */
  uint32_t index;
  uint32_t i;

  if ((length == 0u) || (used >= GNSS_VCP_RING_SIZE) ||
      (total > ((GNSS_VCP_RING_SIZE - 1u) - used)))
  {
    gnss_vcp_lines_dropped++;
    return 0u;
  }

  for (i = 0u; i < length; i++)
  {
    index = (write + i) & (GNSS_VCP_RING_SIZE - 1u);
    mailbox->ring[index] = (uint8_t)line[i];
  }
  index = (write + length) & (GNSS_VCP_RING_SIZE - 1u);
  mailbox->ring[index] = (uint8_t)'\r';
  index = (write + length + 1u) & (GNSS_VCP_RING_SIZE - 1u);
  mailbox->ring[index] = (uint8_t)'\n';

  __DMB(); /* line bytes reach the shared RAM before the new write index */
  mailbox->write = write + total;
  return 1u;
}

/**
  * @brief  Parses a GGA sentence and updates the shared GPS status snapshot.
  * @param  line complete NMEA sentence copied out of the receiver state
  * @note   Updates the GPS status in shared memory using a seqlock protocol.
  *         On valid fix: publishes coordinates, altitude, satellites, timestamp.
  *         On no-fix (quality 0): publishes fix_valid=0 with satellite count.
  *         On invalid GGA: does not modify the existing status (staleness
  *         is handled by the periodic expiration check).
  */
static void GNSS_VCP_UpdateGpsStatus(const char *line)
{
  NmeaGgaFixData fix_data;
  NmeaGgaStatus status;
  uint32_t now_ms = HAL_GetTick();
  GnssVcpGpsStatus *gps_status = GNSS_VCP_GPS_STATUS;

  status = NMEA_GGA_ParseFixData(line, &fix_data);

  if (status == NMEA_GGA_FIX)
  {
    /* Valid fix: publish all fields with current timestamp. */
    GNSS_VCP_PublishGpsStatus(gps_status, now_ms, 1u,
                              fix_data.latitude_udeg,
                              fix_data.longitude_udeg,
                              fix_data.altitude_cm,
                              fix_data.satellites_used);
  }
  else if (status == NMEA_GGA_NO_FIX)
  {
    /* No fix: publish fix_valid=0, keep satellite count for info. */
    GNSS_VCP_PublishGpsStatus(gps_status, now_ms, 0u,
                              0, 0, 0,
                              fix_data.satellites_used);
  }
  /* For NMEA_GGA_NOT_GGA or NMEA_GGA_INVALID, do not update the status.
     The existing fix (if any) will age out via Telemetry_IsFixCurrent. */
}

/**
  * @brief  Checks and expires stale GPS fixes.
  * @note   Call periodically from the main loop. If the current fix is older
  *         than TELEMETRY_MAX_FIX_AGE_MS, it is invalidated. The status is
  *         only rewritten when a fix that claimed to be valid has expired:
  *         re-publishing an already invalid status on every call would keep
  *         the seqlock busy and could starve the CPU2 consumer.
  */
void GNSS_VCP_CheckFixExpiration(void)
{
  GnssVcpGpsStatus *gps_status = GNSS_VCP_GPS_STATUS;
  uint32_t now_ms = HAL_GetTick();
  GnssVcpGpsStatus local_status;

  /* Try to read current status. */
  if (GNSS_VCP_TryReadGpsStatus(gps_status, &local_status))
  {
    if (local_status.fix_valid == 0u)
    {
      return; /* already expired or no fix yet: nothing to publish */
    }

    /* Convert to TelemetryGpsStatus for the expiry check. */
    TelemetryGpsStatus telemetry_status = {
      .fix_valid = local_status.fix_valid,
      .fix_timestamp_ms = local_status.fix_timestamp_ms,
      .latitude_udeg = local_status.latitude_udeg,
      .longitude_udeg = local_status.longitude_udeg,
      .altitude_cm = local_status.altitude_cm,
      .satellites_used = local_status.satellites_used
    };

    if (!Telemetry_IsFixCurrent(&telemetry_status, now_ms))
    {
      /* Fix is stale: publish invalidated status with same timestamp
         (so age keeps increasing) but fix_valid=0. */
      GNSS_VCP_PublishGpsStatus(gps_status, local_status.fix_timestamp_ms, 0u,
                                0, 0, 0, 0u);
    }
  }
}

/**
  * @brief  Queues the human-readable GGA line for the same sentence.
  * @param  line complete NMEA sentence copied out of the receiver state
  * @note   Sentences other than a GGA from the GP/GN talker produce nothing.
  *         Every GGA produces one line: the position for a fix, the no-fix
  *         status with its satellite count for fix quality 0, and the error
  *         line for a malformed or checksum-failed sentence. The raw text of
  *         that sentence is queued above the line only when gnss_vcp_debug is
  *         true.
  */
static void GNSS_VCP_PostGgaLine(const char *line)
{
  char rendered[NMEA_GGA_LINE_CAPACITY];
  NmeaGgaStatus status;
  uint16_t length = 0u;

  status = NMEA_GGA_RenderLine(line, rendered, (uint16_t)sizeof(rendered), &length);

  if (status == NMEA_GGA_INVALID)
  {
    gnss_vcp_gga_invalid++;
  }
  if ((status != NMEA_GGA_NOT_GGA) && (length != 0u))
  {
    (void)GNSS_VCP_Post(rendered, length);
  }
}
