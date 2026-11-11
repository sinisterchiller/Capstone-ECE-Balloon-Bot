/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    telemetry.c
  * @brief   GPS telemetry packet encode/decode implementation.
  *
  * Single source of truth for the wire format used by both CM4 (producer)
  * and CM0+ (consumer on payload, receiver on ground). All multi-byte fields
  * are little-endian. Validation is strict: version, type, length, and all
  * field bounds must pass for a decode to succeed.
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "telemetry.h"

#include <string.h>

/* Private macros ------------------------------------------------------------*/
/* Little-endian write/read helpers for uint16/uint32/int32. */
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

/* Exported functions --------------------------------------------------------*/
bool Telemetry_EncodeGpsStatus(const TelemetryGpsStatus *status,
                               uint16_t sequence,
                               uint8_t *buffer,
                               uint16_t buffer_size)
{
  if ((status == NULL) || (buffer == NULL) || (buffer_size < TELEMETRY_PACKET_SIZE))
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
    /* Latitude: +/-90 degrees -> +/-90,000,000 microdegrees. */
    if ((status->latitude_udeg < -90000000L) || (status->latitude_udeg > 90000000L))
    {
      return false;
    }
    /* Longitude: +/-180 degrees -> +/-180,000,000 microdegrees. */
    if ((status->longitude_udeg < -180000000L) || (status->longitude_udeg > 180000000L))
    {
      return false;
    }
    /* Altitude: roughly -1000 m to +50000 m -> -100000 to +5000000 cm. */
    if ((status->altitude_cm < -100000L) || (status->altitude_cm > 5000000L))
    {
      return false;
    }
  }

  uint8_t *p = buffer;

  *p++ = TELEMETRY_VERSION;
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

  /* Total written: 1+1+2+1+4+4+4+1 = 18 bytes. */
  return true;
}

bool Telemetry_DecodeGpsStatus(const uint8_t *buffer,
                               uint16_t buffer_size,
                               TelemetryGpsStatus *status,
                               uint16_t *sequence)
{
  if ((buffer == NULL) || (buffer_size < TELEMETRY_PACKET_SIZE) ||
      (status == NULL) || (sequence == NULL))
  {
    return false;
  }

  /* Strict length check, as for the ACK: trailing bytes are not this packet
     layout and must not be accepted as a new telemetry sample. */
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