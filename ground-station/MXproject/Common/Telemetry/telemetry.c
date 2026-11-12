/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    telemetry.c
  * @brief   GPS+MPU telemetry packet encode/decode implementation (v3).
  *
  * Single source of truth for the wire format used by both CM4 (producer)
  * and CM0+ (consumer on payload, receiver on ground). All multi-byte fields
  * are little-endian. Validation is strict: version, type, length, and all
  * field bounds must pass for a decode to succeed.
  *
  * v3 packet (26 bytes): GPS fields (18 bytes) + MPU fields (8 bytes).
  *   byte 0: version = 3
  *   byte 1: type = 0x01
  *   bytes 2-3: sequence
  *   byte 4: fix_valid
  *   bytes 5-8: latitude (microdegrees)
  *   bytes 9-12: longitude (microdegrees)
  *   bytes 13-16: altitude (cm)
  *   byte 17: satellites
  *   byte 18: imu_flags (bit0=orient_valid, bit1=heading_valid)
  *   bytes 19-20: heading (degrees * 16, 0 when heading invalid)
  *   bytes 21-22: roll (degrees * 16)
  *   bytes 23-24: pitch (degrees * 16)
  *   byte 25: imu_model (0=unknown, 1=MPU-6500, 2=MPU-9250)
  *
  * v2 packet (BNO055) was replaced by v3 (MPU).
  * v1 packet (GPS only) is deprecated.
  * ACK packet (4 bytes): unchanged.
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "telemetry.h"

#include <string.h>

/* Private macros ------------------------------------------------------------*/
/* Little-endian write/read helpers for uint16/uint32/int32/int16. */
static inline void LE_Write_U16(uint8_t *buf, uint16_t val)
{
  buf[0] = (uint8_t)(val & 0xFFu);
  buf[1] = (uint8_t)((val >> 8) & 0xFFu);
}

static inline void LE_Write_U32(uint8_t *buf, uint32_t val)
{
  buf[0] = (uint8_t)(val & 0xFFu);
  buf[1] = (uint8_t)((val >> 8) & 0xFFu);
  buf[2] = (uint8_t)((val >> 16) & 0xFFu);
  buf[3] = (uint8_t)((val >> 24) & 0xFFu);
}

static inline void LE_Write_I32(uint8_t *buf, int32_t val)
{
  LE_Write_U32(buf, (uint32_t)val);
}

static inline void LE_Write_I16(uint8_t *buf, int16_t val)
{
  buf[0] = (uint8_t)(val & 0xFFu);
  buf[1] = (uint8_t)((val >> 8) & 0xFFu);
}

static inline uint16_t LE_Read_U16(const uint8_t *buf)
{
  return (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
}

static inline uint32_t LE_Read_U32(const uint8_t *buf)
{
  return (uint32_t)buf[0] |
         ((uint32_t)buf[1] << 8) |
         ((uint32_t)buf[2] << 16) |
         ((uint32_t)buf[3] << 24);
}

static inline int32_t LE_Read_I32(const uint8_t *buf)
{
  return (int32_t)LE_Read_U32(buf);
}

static inline int16_t LE_Read_I16(const uint8_t *buf)
{
  return (int16_t)((uint16_t)buf[0] | ((uint16_t)buf[1] << 8));
}

/* Exported functions --------------------------------------------------------*/
bool Telemetry_EncodeCombinedStatus(const TelemetryGpsStatus *gps_status,
                                    const TelemetryMpuStatus *mpu_status,
                                    uint16_t sequence,
                                    uint8_t *buffer,
                                    uint16_t buffer_size)
{
  if ((gps_status == NULL) || (mpu_status == NULL) || (buffer == NULL) || (buffer_size < TELEMETRY_PACKET_SIZE))
  {
    return false;
  }

  /* Validate GPS field bounds when fix is valid. */
  if (gps_status->fix_valid != 0u)
  {
    if (gps_status->satellites_used > TELEMETRY_MAX_SATELLITES)
    {
      return false;
    }
    if ((gps_status->latitude_udeg < -90000000L) || (gps_status->latitude_udeg > 90000000L))
    {
      return false;
    }
    if ((gps_status->longitude_udeg < -180000000L) || (gps_status->longitude_udeg > 180000000L))
    {
      return false;
    }
    if ((gps_status->altitude_cm < -100000L) || (gps_status->altitude_cm > 5000000L))
    {
      return false;
    }
  }

  /* Validate MPU field bounds when valid. */
  if (mpu_status->valid != 0u)
  {
    /* Model must be known when orientation is valid (MPU-6500 or MPU-9250). */
    if ((mpu_status->model != TELEMETRY_IMU_MODEL_MPU6500) &&
        (mpu_status->model != TELEMETRY_IMU_MODEL_MPU9250))
    {
      return false; /* valid orientation requires known model */
    }

    /* heading_valid only allowed for MPU-9250 (model=2). MPU-6500 has no magnetometer. */
    if (mpu_status->heading_valid != 0u)
    {
      if (mpu_status->model != TELEMETRY_IMU_MODEL_MPU9250)
      {
        return false; /* heading_valid only for MPU-9250 */
      }
      if ((mpu_status->heading < 0) || (mpu_status->heading > 5760))
      {
        return false;
      }
    }
    else
    {
      /* When heading is invalid, it must be zero. */
      if (mpu_status->heading != 0)
      {
        return false;
      }
    }
    /* Roll: -180..+180 degrees * 16 = -2880..+2880. */
    if ((mpu_status->roll < -2880) || (mpu_status->roll > 2880))
    {
      return false;
    }
    /* Pitch: -90..+90 degrees * 16 = -1440..+1440. */
    if ((mpu_status->pitch < -1440) || (mpu_status->pitch > 1440))
    {
      return false;
    }
  }

  uint8_t *p = buffer;

  *p++ = TELEMETRY_VERSION;           /* version = 3 */
  *p++ = TELEMETRY_TYPE_GPS_STATUS;   /* type = 0x01 */
  LE_Write_U16(p, sequence);
  p += 2;

  /* GPS fields (bytes 4-17) */
  *p++ = (gps_status->fix_valid != 0u) ? 1u : 0u;

  int32_t lat  = (gps_status->fix_valid != 0u) ? gps_status->latitude_udeg  : 0L;
  int32_t lon  = (gps_status->fix_valid != 0u) ? gps_status->longitude_udeg : 0L;
  int32_t alt  = (gps_status->fix_valid != 0u) ? gps_status->altitude_cm    : 0L;
  uint8_t sats = (gps_status->fix_valid != 0u) ? gps_status->satellites_used : 0u;

  LE_Write_I32(p, lat);
  p += 4;
  LE_Write_I32(p, lon);
  p += 4;
  LE_Write_I32(p, alt);
  p += 4;

  *p++ = sats;

  /* MPU fields (bytes 18-25) */
  uint8_t imu_flags = 0;
  if (mpu_status->valid != 0u)
  {
    imu_flags |= TELEMETRY_IMU_FLAG_ORIENT_VALID;
    if (mpu_status->heading_valid != 0u)
    {
      imu_flags |= TELEMETRY_IMU_FLAG_HEADING_VALID;
    }
  }
  *p++ = imu_flags;

  int16_t hdg  = (mpu_status->valid != 0u && mpu_status->heading_valid != 0u) ? mpu_status->heading : 0;
  int16_t roll = (mpu_status->valid != 0u) ? mpu_status->roll : 0;
  int16_t pitch= (mpu_status->valid != 0u) ? mpu_status->pitch : 0;
  uint8_t model= (mpu_status->valid != 0u) ? mpu_status->model : TELEMETRY_IMU_MODEL_UNKNOWN;

  LE_Write_I16(p, hdg);
  p += 2;
  LE_Write_I16(p, roll);
  p += 2;
  LE_Write_I16(p, pitch);
  p += 2;

  *p++ = model;

  /* Total written: 1+1+2+1+4+4+4+1+1+2+2+2+1 = 26 bytes. */
  return true;
}

bool Telemetry_DecodeCombinedStatus(const uint8_t *buffer,
                                    uint16_t buffer_size,
                                    TelemetryGpsStatus *gps_status,
                                    TelemetryMpuStatus *mpu_status,
                                    uint16_t *sequence)
{
  if ((buffer == NULL) || (buffer_size < TELEMETRY_PACKET_SIZE) ||
      (gps_status == NULL) || (mpu_status == NULL) || (sequence == NULL))
  {
    return false;
  }

  /* Strict length check: trailing bytes are not this packet layout. */
  if (buffer_size != TELEMETRY_PACKET_SIZE)
  {
    return false;
  }

  const uint8_t *p = buffer;

  uint8_t version = *p++;
  uint8_t type    = *p++;

  if (version != TELEMETRY_VERSION)
  {
    return false;
  }
  if (type != TELEMETRY_TYPE_GPS_STATUS)
  {
    return false;
  }

  *sequence = LE_Read_U16(p);
  p += 2;

  /* GPS fields */
  uint8_t fix_valid = *p++;
  if (fix_valid > 1u)
  {
    return false;
  }

  int32_t latitude_udeg  = LE_Read_I32(p); p += 4;
  int32_t longitude_udeg = LE_Read_I32(p); p += 4;
  int32_t altitude_cm    = LE_Read_I32(p); p += 4;
  uint8_t satellites     = *p++;

  /* Validate GPS field bounds when fix is claimed valid. */
  if (fix_valid != 0u)
  {
    if (satellites > TELEMETRY_MAX_SATELLITES)
    {
      return false;
    }
    if ((latitude_udeg < -90000000L) || (latitude_udeg > 90000000L))
    {
      return false;
    }
    if ((longitude_udeg < -180000000L) || (longitude_udeg > 180000000L))
    {
      return false;
    }
    if ((altitude_cm < -100000L) || (altitude_cm > 5000000L))
    {
      return false;
    }
  }
  else
  {
    /* When fix is not valid, coordinate fields must be zero. */
    if ((latitude_udeg != 0L) || (longitude_udeg != 0L) ||
        (altitude_cm != 0L) || (satellites != 0u))
    {
      return false;
    }
  }

  gps_status->fix_valid       = fix_valid;
  gps_status->latitude_udeg   = latitude_udeg;
  gps_status->longitude_udeg  = longitude_udeg;
  gps_status->altitude_cm     = altitude_cm;
  gps_status->satellites_used = satellites;

  /* MPU fields */
  uint8_t imu_flags = *p++;
  if (imu_flags & ~(TELEMETRY_IMU_FLAG_ORIENT_VALID | TELEMETRY_IMU_FLAG_HEADING_VALID))
  {
    return false; /* reserved bits must be zero */
  }

  int16_t heading = LE_Read_I16(p); p += 2;
  int16_t roll    = LE_Read_I16(p); p += 2;
  int16_t pitch   = LE_Read_I16(p); p += 2;
  uint8_t model   = *p++;

  uint8_t orient_valid = (imu_flags & TELEMETRY_IMU_FLAG_ORIENT_VALID) ? 1u : 0u;
  uint8_t heading_valid = (imu_flags & TELEMETRY_IMU_FLAG_HEADING_VALID) ? 1u : 0u;

  /* Validate MPU field bounds when flags claim validity. */
  if (orient_valid != 0u)
  {
    /* Model must be known when orientation is valid (MPU-6500 or MPU-9250). */
    if ((model != TELEMETRY_IMU_MODEL_MPU6500) && (model != TELEMETRY_IMU_MODEL_MPU9250))
    {
      return false; /* valid orientation requires known model */
    }

    /* heading_valid only allowed for MPU-9250 (model=2). MPU-6500 has no magnetometer. */
    if (heading_valid != 0u)
    {
      if (model != TELEMETRY_IMU_MODEL_MPU9250)
      {
        return false; /* heading_valid only for MPU-9250 */
      }
      if ((heading < 0) || (heading > 5760))
      {
        return false;
      }
    }
    else
    {
      /* When heading is invalid, it must be zero. */
      if (heading != 0)
      {
        return false;
      }
    }
    if ((roll < -2880) || (roll > 2880))
    {
      return false;
    }
    if ((pitch < -1440) || (pitch > 1440))
    {
      return false;
    }
  }
  else
  {
    /* When orientation is not valid, all MPU fields must be zero. */
    if ((heading != 0) || (roll != 0) || (pitch != 0) || (model != TELEMETRY_IMU_MODEL_UNKNOWN))
    {
      return false;
    }
    /* heading_valid cannot be set without orient_valid */
    if (heading_valid != 0u)
    {
      return false;
    }
  }

  mpu_status->valid         = orient_valid;
  mpu_status->heading_valid = heading_valid;
  mpu_status->model         = model;
  mpu_status->heading       = heading;
  mpu_status->roll          = roll;
  mpu_status->pitch         = pitch;

  return true;
}

bool Telemetry_EncodeGpsStatus(const TelemetryGpsStatus *status,
                               uint16_t sequence,
                               uint8_t *buffer,
                               uint16_t buffer_size)
{
  if ((status == NULL) || (buffer == NULL) || (buffer_size < TELEMETRY_PACKET_SIZE_V1))
  {
    return false;
  }

  /* Validate field bounds when fix is valid. */
  if (status->fix_valid != 0u)
  {
    if (status->satellites_used > TELEMETRY_MAX_SATELLITES)
    {
      return false;
    }
    if ((status->latitude_udeg < -90000000L) || (status->latitude_udeg > 90000000L))
    {
      return false;
    }
    if ((status->longitude_udeg < -180000000L) || (status->longitude_udeg > 180000000L))
    {
      return false;
    }
    if ((status->altitude_cm < -100000L) || (status->altitude_cm > 5000000L))
    {
      return false;
    }
  }

  uint8_t *p = buffer;

  *p++ = 1u;                          /* v1 version */
  *p++ = TELEMETRY_TYPE_GPS_STATUS;
  LE_Write_U16(p, sequence);
  p += 2;

  *p++ = (status->fix_valid != 0u) ? 1u : 0u;

  int32_t lat  = (status->fix_valid != 0u) ? status->latitude_udeg  : 0L;
  int32_t lon  = (status->fix_valid != 0u) ? status->longitude_udeg : 0L;
  int32_t alt  = (status->fix_valid != 0u) ? status->altitude_cm    : 0L;
  uint8_t sats = (status->fix_valid != 0u) ? status->satellites_used : 0u;

  LE_Write_I32(p, lat);
  p += 4;
  LE_Write_I32(p, lon);
  p += 4;
  LE_Write_I32(p, alt);
  p += 4;

  *p++ = sats;

  /* Total written: 18 bytes (v1). */
  return true;
}

bool Telemetry_DecodeGpsStatus(const uint8_t *buffer,
                               uint16_t buffer_size,
                               TelemetryGpsStatus *status,
                               uint16_t *sequence)
{
  if ((buffer == NULL) || (buffer_size < TELEMETRY_PACKET_SIZE_V1) ||
      (status == NULL) || (sequence == NULL))
  {
    return false;
  }

  /* Strict length check: v1 packet must be exactly v1 size. */
  if (buffer_size != TELEMETRY_PACKET_SIZE_V1)
  {
    return false;
  }

  const uint8_t *p = buffer;

  uint8_t version = *p++;
  uint8_t type    = *p++;

  if (version != 1u)
  {
    return false;
  }
  if (type != TELEMETRY_TYPE_GPS_STATUS)
  {
    return false;
  }

  *sequence = LE_Read_U16(p);
  p += 2;

  uint8_t fix_valid = *p++;
  if (fix_valid > 1u)
  {
    return false;
  }

  int32_t latitude_udeg  = LE_Read_I32(p); p += 4;
  int32_t longitude_udeg = LE_Read_I32(p); p += 4;
  int32_t altitude_cm    = LE_Read_I32(p); p += 4;
  uint8_t satellites     = *p++;

  /* Validate field bounds when fix is claimed valid. */
  if (fix_valid != 0u)
  {
    if (satellites > TELEMETRY_MAX_SATELLITES)
    {
      return false;
    }
    if ((latitude_udeg < -90000000L) || (latitude_udeg > 90000000L))
    {
      return false;
    }
    if ((longitude_udeg < -180000000L) || (longitude_udeg > 180000000L))
    {
      return false;
    }
    if ((altitude_cm < -100000L) || (altitude_cm > 5000000L))
    {
      return false;
    }
  }
  else
  {
    /* When fix is not valid, coordinate fields must be zero. */
    if ((latitude_udeg != 0L) || (longitude_udeg != 0L) ||
        (altitude_cm != 0L) || (satellites != 0u))
    {
      return false;
    }
  }

  status->fix_valid       = fix_valid;
  status->latitude_udeg   = latitude_udeg;
  status->longitude_udeg  = longitude_udeg;
  status->altitude_cm     = altitude_cm;
  status->satellites_used = satellites;

  return true;
}

bool Telemetry_IsFixCurrent(const TelemetryGpsStatus *status, uint32_t now_ms)
{
  if ((status == NULL) || (status->fix_valid == 0u))
  {
    return false;
  }

  uint32_t age_ms = now_ms - status->fix_timestamp_ms;
  /* Modular difference: correct for a counter that wrapped, provided both
     values come from the same clock. Mixing the two cores' clocks (different
     tick rates and origins) makes the difference meaningless in both
     directions, which is why the consumer never calls this function. */
  return (age_ms <= TELEMETRY_MAX_FIX_AGE_MS);
}

void Telemetry_InvalidateFix(TelemetryGpsStatus *status)
{
  if (status != NULL)
  {
    status->fix_valid = 0u;
    /* Coordinate fields are not cleared here; fix_valid == 0 is the
       authoritative indicator. Encoder/decoder treat them as zero when
       fix_valid is clear. */
  }
}

bool Telemetry_EncodeAck(uint16_t sequence, uint8_t *buffer, uint16_t buffer_size)
{
  if ((buffer == NULL) || (buffer_size < TELEMETRY_ACK_PACKET_SIZE))
  {
    return false;
  }

  uint8_t *p = buffer;

  *p++ = TELEMETRY_ACK_VERSION;
  *p++ = TELEMETRY_TYPE_ACK;
  LE_Write_U16(p, sequence);
  p += 2;

  /* Total written: 1+1+2 = 4 bytes. */
  return true;
}

bool Telemetry_DecodeAck(const uint8_t *buffer, uint16_t buffer_size, uint16_t *sequence)
{
  if ((buffer == NULL) || (buffer_size < TELEMETRY_ACK_PACKET_SIZE) || (sequence == NULL))
  {
    return false;
  }

  const uint8_t *p = buffer;

  uint8_t version = *p++;
  uint8_t type    = *p++;

  if (version != TELEMETRY_ACK_VERSION)
  {
    return false;
  }
  if (type != TELEMETRY_TYPE_ACK)
  {
    return false;
  }

  *sequence = LE_Read_U16(p);
  p += 2;

  /* Ensure no extra bytes (strict length check). */
  if (buffer_size != TELEMETRY_ACK_PACKET_SIZE)
  {
    return false;
  }

  return true;
}