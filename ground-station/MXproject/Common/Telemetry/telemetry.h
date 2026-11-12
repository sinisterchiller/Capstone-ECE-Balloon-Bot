/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    telemetry.h
  * @brief   Single wire protocol for GPS+MPU telemetry shared between CM4 and CM0+.
  *
  * Packet layout v3 (fixed, no alignment padding):
  *   uint8_t  version;        // protocol version, must be TELEMETRY_VERSION (3)
  *   uint8_t  type;           // message type, must be TELEMETRY_TYPE_GPS_STATUS
  *   uint16_t sequence;       // monotonically increasing sequence number
  *   uint8_t  fix_valid;      // 1 = valid fix, 0 = no fix / stale
  *   int32_t  latitude;       // degrees * 1,000,000 (microdegrees), signed
  *   int32_t  longitude;      // degrees * 1,000,000 (microdegrees), signed
  *   int32_t  altitude_cm;    // centimetres above MSL, signed
  *   uint8_t  satellites;     // satellites used in solution, 0..255
  *   uint8_t  imu_flags;      // IMU validity flags: bit0=orientation valid, bit1=heading valid
  *   int16_t  heading;        // heading, degrees * 16 (0..360*16), 0 when heading invalid
  *   int16_t  roll;           // roll, degrees * 16 (-180..+180*16)
  *   int16_t  pitch;          // pitch, degrees * 16 (-90..+90*16)
  *   uint8_t  imu_model;      // IMU model identifier: 0=unknown, 1=MPU-6500, 2=MPU-9250
  *
  * Total size: 26 bytes. All multi-byte fields are little-endian.
  *
  * Both cores use the same encode/decode implementation. The CM4 produces
  * packets from the latest validated GPS and MPU status; the CM0+ consumes
  * them for radio transmission. The ground station (CM0+ in GROUND role) decodes
  * and validates received packets.
  *
  * v2 packet (BNO055) was replaced by v3 (MPU).
  * v1 packet (GPS only) is deprecated.
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
#define TELEMETRY_VERSION                 3u

/**
 * @brief Message type for a GPS+MPU status telemetry packet.
 */
#define TELEMETRY_TYPE_GPS_STATUS         0x01u

/**
 * @brief Fixed size of the telemetry packet in bytes (v3).
 *        1 (version) + 1 (type) + 2 (sequence) + 1 (fix_valid) +
 *        4 (latitude) + 4 (longitude) + 4 (altitude) + 1 (satellites) +
 *        1 (imu_flags) + 2 (heading) + 2 (roll) + 2 (pitch) + 1 (imu_model) = 26.
 */
#define TELEMETRY_PACKET_SIZE             26u

/**
 * @brief v2 packet size (for reference/compatibility).
 */
#define TELEMETRY_PACKET_SIZE_V2          26u

/**
 * @brief v1 packet size (for reference/compatibility).
 */
#define TELEMETRY_PACKET_SIZE_V1          18u

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

/**
 * @brief Degrees-to-raw scaling for Euler angles (1/16 degree per LSB).
 */
#define TELEMETRY_DEGREE_SCALE            16L

/* IMU validity flag bits */
#define TELEMETRY_IMU_FLAG_ORIENT_VALID   0x01u  /* bit 0: roll/pitch valid */
#define TELEMETRY_IMU_FLAG_HEADING_VALID  0x02u  /* bit 1: heading valid */

/* IMU model identifiers (wire format) */
#define TELEMETRY_IMU_MODEL_UNKNOWN       0u
#define TELEMETRY_IMU_MODEL_MPU6500       1u
#define TELEMETRY_IMU_MODEL_MPU9250       2u

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

/* Exported types ------------------------------------------------------------*/
/**
 * @brief MPU status for telemetry encoding (non-volatile copy).
 *        Mirrors the fields published by CPU1 and read by CPU2.
 */
typedef struct
{
  uint8_t  valid;           /*!< 1 = orientation (roll/pitch) valid, 0 = invalid / stale */
  uint8_t  heading_valid;   /*!< 1 = heading valid, 0 = unavailable / zero (MPU-6500 never sets this) */
  uint8_t  model;           /*!< TELEMETRY_IMU_MODEL_* device identifier */
  int16_t  heading;         /*!< heading, degrees * 16, 0 when heading_valid is 0 */
  int16_t  roll;            /*!< roll, degrees * 16 (-180..+180*16) */
  int16_t  pitch;           /*!< pitch, degrees * 16 (-90..+90*16) */
} TelemetryMpuStatus;

/* Exported functions --------------------------------------------------------*/
/**
 * @brief Encodes a combined GPS + MPU status into the v3 wire-format packet.
 * @param[in]  gps_status    Source GPS status (fix_valid, coordinates, satellites).
 * @param[in]  mpu_status    Source MPU status (valid, heading_valid, model, heading, roll, pitch).
 * @param[in]  sequence      Telemetry packet sequence number (increments per TX).
 * @param[out] buffer        Destination buffer, must be at least TELEMETRY_PACKET_SIZE bytes.
 * @param[in]  buffer_size   Size of destination buffer in bytes.
 * @return true on success, false if buffer is too small or any status is invalid.
 * @note   When fix_valid is false, GPS coordinate fields are encoded as zero.
 *         When mpu_status.valid is false, MPU fields are encoded as zero and model=unknown.
 *         Both sensors fail independently; the other's data continues.
 *         The function performs bounds checks on all fields.
 *         Heading is encoded as 0 when heading_valid is false.
 */
bool Telemetry_EncodeCombinedStatus(const TelemetryGpsStatus *gps_status,
                                    const TelemetryMpuStatus *mpu_status,
                                    uint16_t sequence,
                                    uint8_t *buffer,
                                    uint16_t buffer_size);

/**
 * @brief Decodes a v3 wire-format packet into GPS and MPU status structures.
 * @param[in]  buffer        Source buffer containing the packet.
 * @param[in]  buffer_size   Size of source buffer in bytes.
 * @param[out] gps_status    Destination GPS status structure.
 * @param[out] mpu_status    Destination MPU status structure.
 * @param[out] sequence      Receives the packet sequence number.
 * @return true on success (valid version, type, length, and field bounds),
 *         false on any validation failure.
 * @note   On failure, the output structures are not modified.
 * @note   The length must be exactly TELEMETRY_PACKET_SIZE: a packet with
 *         trailing bytes is not this protocol version and is rejected.
 */
bool Telemetry_DecodeCombinedStatus(const uint8_t *buffer,
                                    uint16_t buffer_size,
                                    TelemetryGpsStatus *gps_status,
                                    TelemetryMpuStatus *mpu_status,
                                    uint16_t *sequence);

/**
 * @brief Encodes a GPS status into the v1 wire-format packet (legacy/compat).
 * @param[in]  status  Source GPS status (fix_valid, coordinates, satellites).
 * @param[in]  sequence  Telemetry packet sequence number (increments per TX).
 * @param[out] buffer  Destination buffer, must be at least TELEMETRY_PACKET_SIZE_V1 bytes.
 * @param[in]  buffer_size  Size of destination buffer in bytes.
 * @return true on success, false if buffer is too small or status is invalid.
 * @note   When fix_valid is false, coordinate fields are encoded as zero.
 *         The function performs bounds checks on all fields.
 * @deprecated Use Telemetry_EncodeCombinedStatus for v3 packets.
 */
bool Telemetry_EncodeGpsStatus(const TelemetryGpsStatus *status,
                               uint16_t sequence,
                               uint8_t *buffer,
                               uint16_t buffer_size);

/**
 * @brief Decodes a v1 wire-format packet into a GPS status structure (legacy/compat).
 * @param[in]  buffer  Source buffer containing the packet.
 * @param[in]  buffer_size  Size of source buffer in bytes.
 * @param[out] status  Destination GPS status structure.
 * @param[out] sequence  Receives the packet sequence number.
 * @return true on success (valid version, type, length, and field bounds),
 *         false on any validation failure.
 * @note   On failure, the output structures are not modified.
 * @note   The length must be exactly TELEMETRY_PACKET_SIZE_V1.
 * @deprecated Use Telemetry_DecodeCombinedStatus for v3 packets.
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