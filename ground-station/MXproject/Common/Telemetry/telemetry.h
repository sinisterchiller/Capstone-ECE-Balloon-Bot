/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    telemetry.h
  * @brief   Single wire protocol for GPS telemetry shared between CM4 and CM0+.
  *
  * Packet layout (fixed, no alignment padding):
  *   uint8_t  version;        // protocol version, must be TELEMETRY_VERSION
  *   uint8_t  type;           // message type, must be TELEMETRY_TYPE_GPS_STATUS
  *   uint16_t sequence;       // monotonically increasing sequence number
  *   uint8_t  fix_valid;      // 1 = valid fix, 0 = no fix / stale
  *   int32_t  latitude;       // degrees * 1,000,000 (microdegrees), signed
  *   int32_t  longitude;      // degrees * 1,000,000 (microdegrees), signed
  *   int32_t  altitude_cm;    // centimetres above MSL, signed
  *   uint8_t  satellites;     // satellites used in solution, 0..255
  *
  * Total size: 16 bytes. All multi-byte fields are little-endian.
  *
  * Both cores use the same encode/decode implementation. The CM4 produces
  * packets from the latest validated GPS status; the CM0+ consumes them for
  * radio transmission. The ground station (CM0+ in GROUND role) decodes and
  * validates received packets.
  ******************************************************************************
  */
/* USER CODE END Header */
#ifndef __TELEMETRY_H__
#define __TELEMETRY_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include <stdint.h>
#include <stdbool.h>

/* Exported constants --------------------------------------------------------*/
/**
 * @brief Current protocol version. Increment when the packet layout changes.
 */
#define TELEMETRY_VERSION                 1u

/**
 * @brief Message type for a GPS status telemetry packet.
 */
#define TELEMETRY_TYPE_GPS_STATUS         0x01u

/**
 * @brief Fixed size of the telemetry packet in bytes.
 *        1 (version) + 1 (type) + 2 (sequence) + 1 (fix_valid) +
 *        4 (latitude) + 4 (longitude) + 4 (altitude) + 1 (satellites) = 18.
 */
#define TELEMETRY_PACKET_SIZE             18u

/**
 * @brief Message type for an ACK packet (payload -> ground acknowledgment).
 */
#define TELEMETRY_TYPE_ACK                0x02u

/**
 * @brief Fixed size of the ACK packet in bytes.
 *        1 (version) + 1 (type) + 2 (sequence) = 4.
 */
#define TELEMETRY_ACK_PACKET_SIZE         4u

/**
 * @brief ACK packet version (same as telemetry version for protocol consistency).
 */
#define TELEMETRY_ACK_VERSION             TELEMETRY_VERSION

/**
 * @brief Maximum valid satellite count (NMEA GGA field 7 fits in uint8).
 */
#define TELEMETRY_MAX_SATELLITES          255u

/**
 * @brief Microdegrees per degree (1,000,000).
 */
#define TELEMETRY_MICRODEGREES_PER_DEGREE 1000000L

/**
 * @brief Centimetres per metre (100).
 */
#define TELEMETRY_CM_PER_METRE            100L

/* Exported types ------------------------------------------------------------*/
/**
 * @brief Latest validated GPS status, maintained by CM4 and readable by CM0+.
 *
 * This structure lives in shared SRAM1 (alongside the existing mailbox ring)
 * and is updated atomically from the CM4 main loop. CM0+ reads it without
 * locking; memory ordering is enforced by DMB barriers on both sides.
 *
 * All fields describe the producer's view. A fix is considered stale if its
 * age exceeds TELEMETRY_MAX_FIX_AGE_MS; in that case fix_valid is cleared and
 * the coordinate fields are undefined.
 *
 * Clock domains: fix_timestamp_ms and the age checks around it use the
 * producer's clock only. A consumer on the other core must not compare it with
 * its own time (the two cores have different tick rates and origins); it
 * measures the age of the publication on its own clock instead, see
 * Common/Telemetry/telemetry_gps_status.h.
 */
typedef struct
{
  /* Sequence number of the publication this status was taken from: the last
     GGA sentence that produced a valid fix on the producer, the seqlock
     publication sequence on the consumer. Monotonic; wraps at UINT32_MAX. */
  volatile uint32_t fix_sequence;

  /* Timestamp (producer's HAL_GetTick()) when the fix was validated. Producer
     clock domain: meaningful only when compared with another producer stamp. */
  volatile uint32_t fix_timestamp_ms;

  /* Fix validity: 1 = current valid fix, 0 = no fix or stale. */
  volatile uint8_t  fix_valid;

  /* Latitude in microdegrees (degrees * 1,000,000), signed. Valid only when fix_valid == 1. */
  volatile int32_t  latitude_udeg;

  /* Longitude in microdegrees (degrees * 1,000,000), signed. Valid only when fix_valid == 1. */
  volatile int32_t  longitude_udeg;

  /* Altitude above mean sea level in centimetres, signed. Valid only when fix_valid == 1. */
  volatile int32_t  altitude_cm;

  /* Satellites used in the solution (GGA field 7). Valid only when fix_valid == 1. */
  volatile uint8_t  satellites_used;

  /* Padding to 16-byte alignment for cleaner shared-memory layout. */
  volatile uint8_t  _reserved[3];
} TelemetryGpsStatus;

/* Exported constants --------------------------------------------------------*/
/**
 * @brief Maximum age of a GPS fix before it is considered stale (milliseconds).
 *        30 seconds covers the 20 s Low Power sampling interval plus margin.
 */
#define TELEMETRY_MAX_FIX_AGE_MS          30000u

/* Exported functions --------------------------------------------------------*/
/**
 * @brief Encodes a GPS status into the fixed wire-format packet.
 * @param[in]  status  Source GPS status (fix_valid, coordinates, satellites).
 * @param[in]  sequence  Telemetry packet sequence number (increments per TX).
 * @param[out] buffer  Destination buffer, must be at least TELEMETRY_PACKET_SIZE bytes.
 * @param[in]  buffer_size  Size of destination buffer in bytes.
 * @return true on success, false if buffer is too small or status is invalid.
 * @note   When fix_valid is false, coordinate fields are encoded as zero.
 *         The function performs bounds checks on all fields.
 */
bool Telemetry_EncodeGpsStatus(const TelemetryGpsStatus *status,
                               uint16_t sequence,
                               uint8_t *buffer,
                               uint16_t buffer_size);

/**
 * @brief Decodes a wire-format packet into a GPS status structure.
 * @param[in]  buffer  Source buffer containing the packet.
 * @param[in]  buffer_size  Size of source buffer in bytes.
 * @param[out] status  Destination GPS status structure.
 * @param[out] sequence  Receives the packet sequence number.
 * @return true on success (valid version, type, length, and field bounds),
 *         false on any validation failure.
 * @note   On failure, the output structures are not modified.
 * @note   The length must be exactly TELEMETRY_PACKET_SIZE: a packet with
 *         trailing bytes is not this protocol version and is rejected.
 */
bool Telemetry_DecodeGpsStatus(const uint8_t *buffer,
                               uint16_t buffer_size,
                               TelemetryGpsStatus *status,
                               uint16_t *sequence);

/**
 * @brief Checks whether a GPS fix is still current based on its timestamp.
 * @param[in]  status  GPS status with fix_timestamp_ms populated.
 * @param[in]  now_ms  Current time of the same clock that stamped the fix, i.e.
 *                     the producer's HAL_GetTick() on the core that validated
 *                     it. A consumer on the other core must not call this with
 *                     its own clock; it uses Telemetry_ReadGpsStatusSnapshot()
 *                     instead, which ages the publication on the consumer's
 *                     clock.
 * @return true if fix_valid is set and age <= TELEMETRY_MAX_FIX_AGE_MS,
 *         false otherwise.
 * @note   Handles uint32_t wrap-around of the tick counter correctly.
 */
bool Telemetry_IsFixCurrent(const TelemetryGpsStatus *status, uint32_t now_ms);

/**
 * @brief Invalidates a GPS fix (marks as no-fix / stale).
 * @param[in,out] status  GPS status to invalidate.
 */
void Telemetry_InvalidateFix(TelemetryGpsStatus *status);

/**
 * @brief Encodes an ACK packet with the given sequence number.
 * @param[in]  sequence  Sequence number to acknowledge (copied from telemetry packet).
 * @param[out] buffer  Destination buffer, must be at least TELEMETRY_ACK_PACKET_SIZE bytes.
 * @param[in]  buffer_size  Size of destination buffer in bytes.
 * @return true on success, false if buffer is too small.
 */
bool Telemetry_EncodeAck(uint16_t sequence, uint8_t *buffer, uint16_t buffer_size);

/**
 * @brief Decodes an ACK packet and extracts the sequence number.
 * @param[in]  buffer  Source buffer containing the packet.
 * @param[in]  buffer_size  Size of source buffer in bytes.
 * @param[out] sequence  Receives the acknowledged sequence number.
 * @return true on success (valid version, type, length), false on any validation failure.
 * @note   On failure, the output sequence is not modified.
 */
bool Telemetry_DecodeAck(const uint8_t *buffer, uint16_t buffer_size, uint16_t *sequence);

#ifdef __cplusplus
}
#endif

#endif /* __TELEMETRY_H__ */