/*
 * Host tests for Common/Telemetry/telemetry.c.
 *
 * The codec is pure C with no HAL dependency, so it runs unchanged on the
 * development machine. Build and run from the MXproject directory:
 *
 *   cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
 *      -I Common/Telemetry -I CM4/Core/Inc \
 *      Common/Telemetry/telemetry.c Common/Telemetry/telemetry_test.c \
 *      -o /tmp/telemetry_test && /tmp/telemetry_test
 *
 * Tests v3 (26-byte combined GPS+MPU) and v1 (18-byte GPS only) packets,
 * plus ACK packets (4 bytes).
 */

#include "telemetry.h"

#include <stdio.h>
#include <string.h>

static unsigned int failures;

/* v1 reference packets (18 bytes) */
static const uint8_t REF_V1_PACKET_FIX[] = {
    0x01,                   /* version = 1 */
    0x01,                   /* type = GPS_STATUS */
    0x2A, 0x00,             /* sequence = 42 (little-endian) */
    0x01,                   /* fix_valid = 1 */
    0x34, 0x36, 0xDE, 0x02, /* latitude = 48117300 (little-endian) */
    0xFB, 0xBA, 0xAF, 0x00, /* longitude = 11516667 (little-endian) */
    0x0C, 0xD5, 0x00, 0x00, /* altitude = 54540 cm (little-endian) */
    0x08                    /* satellites = 8 */
};
_Static_assert(sizeof(REF_V1_PACKET_FIX) == TELEMETRY_PACKET_SIZE_V1, "REF_V1_PACKET_FIX size mismatch");

static const uint8_t REF_V1_PACKET_NO_FIX[] = {
    0x01,                   /* version = 1 */
    0x01,                   /* type = GPS_STATUS */
    0x2B, 0x00,             /* sequence = 43 */
    0x00,                   /* fix_valid = 0 */
    0x00, 0x00, 0x00, 0x00, /* latitude = 0 */
    0x00, 0x00, 0x00, 0x00, /* longitude = 0 */
    0x00, 0x00, 0x00, 0x00, /* altitude = 0 */
    0x00                    /* satellites = 0 */
};
_Static_assert(sizeof(REF_V1_PACKET_NO_FIX) == TELEMETRY_PACKET_SIZE_V1, "REF_V1_PACKET_NO_FIX size mismatch");

/* v3 reference packets (26 bytes) */
/* GPS: 48.117300 N, 11.516667 E, 545.4 m, 8 sats */
/* MPU: MPU-9250, heading=125.50 deg (2008 raw), roll=-4.25 deg (-68 raw), pitch=12.75 deg (204 raw) */
static const uint8_t REF_V3_PACKET_FIX[] = {
    0x03,                   /* version = 3 */
    0x01,                   /* type = GPS_STATUS */
    0x2A, 0x00,             /* sequence = 42 (little-endian) */
    0x01,                   /* fix_valid = 1 */
    0x34, 0x36, 0xDE, 0x02, /* latitude = 48117300 */
    0xFB, 0xBA, 0xAF, 0x00, /* longitude = 11516667 */
    0x0C, 0xD5, 0x00, 0x00, /* altitude = 54540 */
    0x08,                   /* satellites = 8 */
    0x03,                   /* imu_flags = orient_valid(1) | heading_valid(1) = 3 */
    0xD8, 0x07,             /* heading = 2008 (125.50 * 16) */
    0xBC, 0xFF,             /* roll = -68 (-4.25 * 16) */
    0xCC, 0x00,             /* pitch = 204 (12.75 * 16) */
    0x02                    /* imu_model = 2 (MPU-9250) */
};
_Static_assert(sizeof(REF_V3_PACKET_FIX) == TELEMETRY_PACKET_SIZE, "REF_V3_PACKET_FIX size mismatch");

/* GPS only, MPU invalid */
static const uint8_t REF_V3_PACKET_GPS_ONLY[] = {
    0x03,                   /* version = 3 */
    0x01,                   /* type = GPS_STATUS */
    0x2B, 0x00,             /* sequence = 43 */
    0x01,                   /* fix_valid = 1 */
    0x34, 0x36, 0xDE, 0x02, /* latitude = 48117300 */
    0xFB, 0xBA, 0xAF, 0x00, /* longitude = 11516667 */
    0x0C, 0xD5, 0x00, 0x00, /* altitude = 54540 */
    0x08,                   /* satellites = 8 */
    0x00,                   /* imu_flags = 0 */
    0x00, 0x00,             /* heading = 0 */
    0x00, 0x00,             /* roll = 0 */
    0x00, 0x00,             /* pitch = 0 */
    0x00                    /* imu_model = 0 (unknown) */
};
_Static_assert(sizeof(REF_V3_PACKET_GPS_ONLY) == TELEMETRY_PACKET_SIZE, "REF_V3_PACKET_GPS_ONLY size mismatch");

/* MPU only (MPU-6500, no heading), GPS invalid */
static const uint8_t REF_V3_PACKET_MPU_ONLY[] = {
    0x03,                   /* version = 3 */
    0x01,                   /* type = GPS_STATUS */
    0x2C, 0x00,             /* sequence = 44 */
    0x00,                   /* fix_valid = 0 */
    0x00, 0x00, 0x00, 0x00, /* latitude = 0 */
    0x00, 0x00, 0x00, 0x00, /* longitude = 0 */
    0x00, 0x00, 0x00, 0x00, /* altitude = 0 */
    0x00,                   /* satellites = 0 */
    0x01,                   /* imu_flags = orient_valid only (bit0) */
    0x00, 0x00,             /* heading = 0 (heading_valid=0) */
    0xBC, 0xFF,             /* roll = -68 */
    0xCC, 0x00,             /* pitch = 204 */
    0x01                    /* imu_model = 1 (MPU-6500) */
};
_Static_assert(sizeof(REF_V3_PACKET_MPU_ONLY) == TELEMETRY_PACKET_SIZE, "REF_V3_PACKET_MPU_ONLY size mismatch");

/* Neither GPS nor MPU valid */
static const uint8_t REF_V3_PACKET_NONE[] = {
    0x03,                   /* version = 3 */
    0x01,                   /* type = GPS_STATUS */
    0x2D, 0x00,             /* sequence = 45 */
    0x00,                   /* fix_valid = 0 */
    0x00, 0x00, 0x00, 0x00, /* latitude = 0 */
    0x00, 0x00, 0x00, 0x00, /* longitude = 0 */
    0x00, 0x00, 0x00, 0x00, /* altitude = 0 */
    0x00,                   /* satellites = 0 */
    0x00,                   /* imu_flags = 0 */
    0x00, 0x00,             /* heading = 0 */
    0x00, 0x00,             /* roll = 0 */
    0x00, 0x00,             /* pitch = 0 */
    0x00                    /* imu_model = 0 */
};
_Static_assert(sizeof(REF_V3_PACKET_NONE) == TELEMETRY_PACKET_SIZE, "REF_V3_PACKET_NONE size mismatch");

/* Reference ACK packet for sequence 42 (little-endian). */
static const uint8_t REF_ACK_PACKET[] = {
    0x03,                   /* version = 3 (matches telemetry version) */
    0x02,                   /* type = ACK */
    0x2A, 0x00              /* sequence = 42 (little-endian) */
};
_Static_assert(sizeof(REF_ACK_PACKET) == TELEMETRY_ACK_PACKET_SIZE, "REF_ACK_PACKET size mismatch");

static void check_v1_encode_fix(void)
{
  TelemetryGpsStatus status = {0};
  uint8_t buffer[TELEMETRY_PACKET_SIZE_V1];
  bool ok;

  status.fix_valid = 1;
  status.latitude_udeg = 48117300;
  status.longitude_udeg = 11516667;
  status.altitude_cm = 54540;
  status.satellites_used = 8;
  status.fix_timestamp_ms = 12345;
  status.fix_sequence = 1;

  ok = Telemetry_EncodeGpsStatus(&status, 42, buffer, sizeof(buffer));
  if (!ok)
  {
    printf("FAIL v1_encode_fix: Telemetry_EncodeGpsStatus returned false\n");
    failures++;
    return;
  }
  if (memcmp(buffer, REF_V1_PACKET_FIX, TELEMETRY_PACKET_SIZE_V1) != 0)
  {
    printf("FAIL v1_encode_fix: packet mismatch\n");
    for (uint16_t i = 0; i < TELEMETRY_PACKET_SIZE_V1; i++)
    {
      if (buffer[i] != REF_V1_PACKET_FIX[i])
      {
        printf("  byte %u: got 0x%02X, expected 0x%02X\n", i, buffer[i], REF_V1_PACKET_FIX[i]);
      }
    }
    failures++;
    return;
  }
}

static void check_v1_encode_no_fix(void)
{
  TelemetryGpsStatus status = {0};
  uint8_t buffer[TELEMETRY_PACKET_SIZE_V1];
  bool ok;

  status.fix_valid = 0;
  status.latitude_udeg = 12345;      /* should be ignored */
  status.longitude_udeg = 67890;     /* should be ignored */
  status.altitude_cm = 100;          /* should be ignored */
  status.satellites_used = 5;        /* should be ignored */
  status.fix_timestamp_ms = 12345;
  status.fix_sequence = 1;

  ok = Telemetry_EncodeGpsStatus(&status, 43, buffer, sizeof(buffer));
  if (!ok)
  {
    printf("FAIL v1_encode_no_fix: Telemetry_EncodeGpsStatus returned false\n");
    failures++;
    return;
  }
  if (memcmp(buffer, REF_V1_PACKET_NO_FIX, TELEMETRY_PACKET_SIZE_V1) != 0)
  {
    printf("FAIL v1_encode_no_fix: packet mismatch\n");
    for (uint16_t i = 0; i < TELEMETRY_PACKET_SIZE_V1; i++)
    {
      if (buffer[i] != REF_V1_PACKET_NO_FIX[i])
      {
        printf("  byte %u: got 0x%02X, expected 0x%02X\n", i, buffer[i], REF_V1_PACKET_NO_FIX[i]);
      }
    }
    failures++;
    return;
  }
}

static void check_v3_encode_combined_fix(void)
{
  TelemetryGpsStatus gps_status = {0};
  TelemetryMpuStatus mpu_status = {0};
  uint8_t buffer[TELEMETRY_PACKET_SIZE];
  bool ok;

  gps_status.fix_valid = 1;
  gps_status.latitude_udeg = 48117300;
  gps_status.longitude_udeg = 11516667;
  gps_status.altitude_cm = 54540;
  gps_status.satellites_used = 8;

  mpu_status.valid = 1;
  mpu_status.heading_valid = 1;
  mpu_status.model = TELEMETRY_IMU_MODEL_MPU9250;
  mpu_status.heading = 2008;   /* 125.50 * 16 */
  mpu_status.roll = -68;       /* -4.25 * 16 */
  mpu_status.pitch = 204;      /* 12.75 * 16 */

  ok = Telemetry_EncodeCombinedStatus(&gps_status, &mpu_status, 42, buffer, sizeof(buffer));
  if (!ok)
  {
    printf("FAIL v3_encode_combined_fix: Telemetry_EncodeCombinedStatus returned false\n");
    failures++;
    return;
  }
  if (memcmp(buffer, REF_V3_PACKET_FIX, TELEMETRY_PACKET_SIZE) != 0)
  {
    printf("FAIL v3_encode_combined_fix: packet mismatch\n");
    for (uint16_t i = 0; i < TELEMETRY_PACKET_SIZE; i++)
    {
      if (buffer[i] != REF_V3_PACKET_FIX[i])
      {
        printf("  byte %u: got 0x%02X, expected 0x%02X\n", i, buffer[i], REF_V3_PACKET_FIX[i]);
      }
    }
    failures++;
    return;
  }
}

static void check_v3_encode_gps_only(void)
{
  TelemetryGpsStatus gps_status = {0};
  TelemetryMpuStatus mpu_status = {0};
  uint8_t buffer[TELEMETRY_PACKET_SIZE];
  bool ok;

  gps_status.fix_valid = 1;
  gps_status.latitude_udeg = 48117300;
  gps_status.longitude_udeg = 11516667;
  gps_status.altitude_cm = 54540;
  gps_status.satellites_used = 8;

  mpu_status.valid = 0;
  mpu_status.heading_valid = 0;
  mpu_status.model = TELEMETRY_IMU_MODEL_UNKNOWN;
  mpu_status.heading = 0;
  mpu_status.roll = 0;
  mpu_status.pitch = 0;

  ok = Telemetry_EncodeCombinedStatus(&gps_status, &mpu_status, 43, buffer, sizeof(buffer));
  if (!ok)
  {
    printf("FAIL v3_encode_gps_only: Telemetry_EncodeCombinedStatus returned false\n");
    failures++;
    return;
  }
  if (memcmp(buffer, REF_V3_PACKET_GPS_ONLY, TELEMETRY_PACKET_SIZE) != 0)
  {
    printf("FAIL v3_encode_gps_only: packet mismatch\n");
    for (uint16_t i = 0; i < TELEMETRY_PACKET_SIZE; i++)
    {
      if (buffer[i] != REF_V3_PACKET_GPS_ONLY[i])
      {
        printf("  byte %u: got 0x%02X, expected 0x%02X\n", i, buffer[i], REF_V3_PACKET_GPS_ONLY[i]);
      }
    }
    failures++;
    return;
  }
}

static void check_v3_encode_mpu_only(void)
{
  TelemetryGpsStatus gps_status = {0};
  TelemetryMpuStatus mpu_status = {0};
  uint8_t buffer[TELEMETRY_PACKET_SIZE];
  bool ok;

  gps_status.fix_valid = 0;
  gps_status.latitude_udeg = 0;
  gps_status.longitude_udeg = 0;
  gps_status.altitude_cm = 0;
  gps_status.satellites_used = 0;

  mpu_status.valid = 1;
  mpu_status.heading_valid = 0;  /* MPU-6500: no heading */
  mpu_status.model = TELEMETRY_IMU_MODEL_MPU6500;
  mpu_status.heading = 0;
  mpu_status.roll = -68;
  mpu_status.pitch = 204;

  ok = Telemetry_EncodeCombinedStatus(&gps_status, &mpu_status, 44, buffer, sizeof(buffer));
  if (!ok)
  {
    printf("FAIL v3_encode_mpu_only: Telemetry_EncodeCombinedStatus returned false\n");
    failures++;
    return;
  }
  if (memcmp(buffer, REF_V3_PACKET_MPU_ONLY, TELEMETRY_PACKET_SIZE) != 0)
  {
    printf("FAIL v3_encode_mpu_only: packet mismatch\n");
    for (uint16_t i = 0; i < TELEMETRY_PACKET_SIZE; i++)
    {
      if (buffer[i] != REF_V3_PACKET_MPU_ONLY[i])
      {
        printf("  byte %u: got 0x%02X, expected 0x%02X\n", i, buffer[i], REF_V3_PACKET_MPU_ONLY[i]);
      }
    }
    failures++;
    return;
  }
}

static void check_v3_encode_none(void)
{
  TelemetryGpsStatus gps_status = {0};
  TelemetryMpuStatus mpu_status = {0};
  uint8_t buffer[TELEMETRY_PACKET_SIZE];
  bool ok;

  gps_status.fix_valid = 0;
  mpu_status.valid = 0;

  ok = Telemetry_EncodeCombinedStatus(&gps_status, &mpu_status, 45, buffer, sizeof(buffer));
  if (!ok)
  {
    printf("FAIL v3_encode_none: Telemetry_EncodeCombinedStatus returned false\n");
    failures++;
    return;
  }
  if (memcmp(buffer, REF_V3_PACKET_NONE, TELEMETRY_PACKET_SIZE) != 0)
  {
    printf("FAIL v3_encode_none: packet mismatch\n");
    for (uint16_t i = 0; i < TELEMETRY_PACKET_SIZE; i++)
    {
      if (buffer[i] != REF_V3_PACKET_NONE[i])
      {
        printf("  byte %u: got 0x%02X, expected 0x%02X\n", i, buffer[i], REF_V3_PACKET_NONE[i]);
      }
    }
    failures++;
    return;
  }
}

static void check_v1_decode_fix(void)
{
  TelemetryGpsStatus status = {0};
  uint16_t sequence = 0;
  bool ok;

  ok = Telemetry_DecodeGpsStatus(REF_V1_PACKET_FIX, sizeof(REF_V1_PACKET_FIX), &status, &sequence);
  if (!ok)
  {
    printf("FAIL v1_decode_fix: Telemetry_DecodeGpsStatus returned false\n");
    failures++;
    return;
  }
  if (sequence != 42)
  {
    printf("FAIL v1_decode_fix: sequence %u, expected 42\n", sequence);
    failures++;
  }
  if (status.fix_valid != 1)
  {
    printf("FAIL v1_decode_fix: fix_valid %u, expected 1\n", status.fix_valid);
    failures++;
  }
  if (status.latitude_udeg != 48117300)
  {
    printf("FAIL v1_decode_fix: latitude %d, expected 48117300\n", status.latitude_udeg);
    failures++;
  }
  if (status.longitude_udeg != 11516667)
  {
    printf("FAIL v1_decode_fix: longitude %d, expected 11516667\n", status.longitude_udeg);
    failures++;
  }
  if (status.altitude_cm != 54540)
  {
    printf("FAIL v1_decode_fix: altitude %d, expected 54540\n", status.altitude_cm);
    failures++;
  }
  if (status.satellites_used != 8)
  {
    printf("FAIL v1_decode_fix: satellites %u, expected 8\n", status.satellites_used);
    failures++;
  }
}

static void check_v1_decode_no_fix(void)
{
  TelemetryGpsStatus status = {0};
  uint16_t sequence = 0;
  bool ok;

  ok = Telemetry_DecodeGpsStatus(REF_V1_PACKET_NO_FIX, sizeof(REF_V1_PACKET_NO_FIX), &status, &sequence);
  if (!ok)
  {
    printf("FAIL v1_decode_no_fix: Telemetry_DecodeGpsStatus returned false\n");
    failures++;
    return;
  }
  if (sequence != 43)
  {
    printf("FAIL v1_decode_no_fix: sequence %u, expected 43\n", sequence);
    failures++;
  }
  if (status.fix_valid != 0)
  {
    printf("FAIL v1_decode_no_fix: fix_valid %u, expected 0\n", status.fix_valid);
    failures++;
  }
  if (status.latitude_udeg != 0)
  {
    printf("FAIL v1_decode_no_fix: latitude %d, expected 0\n", status.latitude_udeg);
    failures++;
  }
  if (status.longitude_udeg != 0)
  {
    printf("FAIL v1_decode_no_fix: longitude %d, expected 0\n", status.longitude_udeg);
    failures++;
  }
  if (status.altitude_cm != 0)
  {
    printf("FAIL v1_decode_no_fix: altitude %d, expected 0\n", status.altitude_cm);
    failures++;
  }
  if (status.satellites_used != 0)
  {
    printf("FAIL v1_decode_no_fix: satellites %u, expected 0\n", status.satellites_used);
    failures++;
  }
}

static void check_v3_decode_combined_fix(void)
{
  TelemetryGpsStatus gps_status = {0};
  TelemetryMpuStatus mpu_status = {0};
  uint16_t sequence = 0;
  bool ok;

  ok = Telemetry_DecodeCombinedStatus(REF_V3_PACKET_FIX, sizeof(REF_V3_PACKET_FIX), &gps_status, &mpu_status, &sequence);
  if (!ok)
  {
    printf("FAIL v3_decode_combined_fix: Telemetry_DecodeCombinedStatus returned false\n");
    failures++;
    return;
  }
  if (sequence != 42)
  {
    printf("FAIL v3_decode_combined_fix: sequence %u, expected 42\n", sequence);
    failures++;
  }
  if (gps_status.fix_valid != 1)
  {
    printf("FAIL v3_decode_combined_fix: fix_valid %u, expected 1\n", gps_status.fix_valid);
    failures++;
  }
  if (gps_status.latitude_udeg != 48117300)
  {
    printf("FAIL v3_decode_combined_fix: latitude %d, expected 48117300\n", gps_status.latitude_udeg);
    failures++;
  }
  if (gps_status.longitude_udeg != 11516667)
  {
    printf("FAIL v3_decode_combined_fix: longitude %d, expected 11516667\n", gps_status.longitude_udeg);
    failures++;
  }
  if (gps_status.altitude_cm != 54540)
  {
    printf("FAIL v3_decode_combined_fix: altitude %d, expected 54540\n", gps_status.altitude_cm);
    failures++;
  }
  if (gps_status.satellites_used != 8)
  {
    printf("FAIL v3_decode_combined_fix: satellites %u, expected 8\n", gps_status.satellites_used);
    failures++;
  }
  if (mpu_status.valid != 1)
  {
    printf("FAIL v3_decode_combined_fix: mpu_valid %u, expected 1\n", mpu_status.valid);
    failures++;
  }
  if (mpu_status.heading_valid != 1)
  {
    printf("FAIL v3_decode_combined_fix: heading_valid %u, expected 1\n", mpu_status.heading_valid);
    failures++;
  }
  if (mpu_status.heading != 2008)
  {
    printf("FAIL v3_decode_combined_fix: heading %d, expected 2008\n", mpu_status.heading);
    failures++;
  }
  if (mpu_status.roll != -68)
  {
    printf("FAIL v3_decode_combined_fix: roll %d, expected -68\n", mpu_status.roll);
    failures++;
  }
  if (mpu_status.pitch != 204)
  {
    printf("FAIL v3_decode_combined_fix: pitch %d, expected 204\n", mpu_status.pitch);
    failures++;
  }
  if (mpu_status.model != TELEMETRY_IMU_MODEL_MPU9250)
  {
    printf("FAIL v3_decode_combined_fix: model %u, expected %u\n", mpu_status.model, TELEMETRY_IMU_MODEL_MPU9250);
    failures++;
  }
}

static void check_v3_decode_gps_only(void)
{
  TelemetryGpsStatus gps_status = {0};
  TelemetryMpuStatus mpu_status = {0};
  uint16_t sequence = 0;
  bool ok;

  ok = Telemetry_DecodeCombinedStatus(REF_V3_PACKET_GPS_ONLY, sizeof(REF_V3_PACKET_GPS_ONLY), &gps_status, &mpu_status, &sequence);
  if (!ok)
  {
    printf("FAIL v3_decode_gps_only: Telemetry_DecodeCombinedStatus returned false\n");
    failures++;
    return;
  }
  if (sequence != 43)
  {
    printf("FAIL v3_decode_gps_only: sequence %u, expected 43\n", sequence);
    failures++;
  }
  if (gps_status.fix_valid != 1)
  {
    printf("FAIL v3_decode_gps_only: fix_valid %u, expected 1\n", gps_status.fix_valid);
    failures++;
  }
  if (mpu_status.valid != 0)
  {
    printf("FAIL v3_decode_gps_only: mpu_valid %u, expected 0\n", mpu_status.valid);
    failures++;
  }
  if (mpu_status.heading_valid != 0 || mpu_status.heading != 0 || mpu_status.roll != 0 || mpu_status.pitch != 0 || mpu_status.model != TELEMETRY_IMU_MODEL_UNKNOWN)
  {
    printf("FAIL v3_decode_gps_only: MPU fields not zero/unknown when invalid\n");
    failures++;
  }
}

static void check_v3_decode_mpu_only(void)
{
  TelemetryGpsStatus gps_status = {0};
  TelemetryMpuStatus mpu_status = {0};
  uint16_t sequence = 0;
  bool ok;

  ok = Telemetry_DecodeCombinedStatus(REF_V3_PACKET_MPU_ONLY, sizeof(REF_V3_PACKET_MPU_ONLY), &gps_status, &mpu_status, &sequence);
  if (!ok)
  {
    printf("FAIL v3_decode_mpu_only: Telemetry_DecodeCombinedStatus returned false\n");
    failures++;
    return;
  }
  if (sequence != 44)
  {
    printf("FAIL v3_decode_mpu_only: sequence %u, expected 44\n", sequence);
    failures++;
  }
  if (gps_status.fix_valid != 0)
  {
    printf("FAIL v3_decode_mpu_only: fix_valid %u, expected 0\n", gps_status.fix_valid);
    failures++;
  }
  if (mpu_status.valid != 1)
  {
    printf("FAIL v3_decode_mpu_only: mpu_valid %u, expected 1\n", mpu_status.valid);
    failures++;
  }
  if (mpu_status.heading_valid != 0)
  {
    printf("FAIL v3_decode_mpu_only: heading_valid %u, expected 0 (MPU-6500)\n", mpu_status.heading_valid);
    failures++;
  }
  if (mpu_status.heading != 0)
  {
    printf("FAIL v3_decode_mpu_only: heading %d, expected 0\n", mpu_status.heading);
    failures++;
  }
  if (mpu_status.roll != -68 || mpu_status.pitch != 204)
  {
    printf("FAIL v3_decode_mpu_only: roll/pitch mismatch\n");
    failures++;
  }
  if (mpu_status.model != TELEMETRY_IMU_MODEL_MPU6500)
  {
    printf("FAIL v3_decode_mpu_only: model %u, expected %u\n", mpu_status.model, TELEMETRY_IMU_MODEL_MPU6500);
    failures++;
  }
}

static void check_v3_decode_none(void)
{
  TelemetryGpsStatus gps_status = {0};
  TelemetryMpuStatus mpu_status = {0};
  uint16_t sequence = 0;
  bool ok;

  ok = Telemetry_DecodeCombinedStatus(REF_V3_PACKET_NONE, sizeof(REF_V3_PACKET_NONE), &gps_status, &mpu_status, &sequence);
  if (!ok)
  {
    printf("FAIL v3_decode_none: Telemetry_DecodeCombinedStatus returned false\n");
    failures++;
    return;
  }
  if (sequence != 45)
  {
    printf("FAIL v3_decode_none: sequence %u, expected 45\n", sequence);
    failures++;
  }
  if (gps_status.fix_valid != 0 || mpu_status.valid != 0)
  {
    printf("FAIL v3_decode_none: validity flags not zero\n");
    failures++;
  }
}

static void check_v1_version_mismatch(void)
{
  TelemetryGpsStatus status = {0};
  uint16_t sequence = 0;
  uint8_t bad_packet[TELEMETRY_PACKET_SIZE_V1];
  bool ok;

  memcpy(bad_packet, REF_V1_PACKET_FIX, TELEMETRY_PACKET_SIZE_V1);
  bad_packet[0] = 0xFF; /* wrong version */

  ok = Telemetry_DecodeGpsStatus(bad_packet, TELEMETRY_PACKET_SIZE_V1, &status, &sequence);
  if (ok)
  {
    printf("FAIL v1_version_mismatch: should have rejected wrong version\n");
    failures++;
  }
}

static void check_v3_version_mismatch(void)
{
  TelemetryGpsStatus gps_status = {0};
  TelemetryMpuStatus mpu_status = {0};
  uint16_t sequence = 0;
  uint8_t bad_packet[TELEMETRY_PACKET_SIZE];
  bool ok;

  memcpy(bad_packet, REF_V3_PACKET_FIX, TELEMETRY_PACKET_SIZE);
  bad_packet[0] = 0xFF; /* wrong version */

  ok = Telemetry_DecodeCombinedStatus(bad_packet, TELEMETRY_PACKET_SIZE, &gps_status, &mpu_status, &sequence);
  if (ok)
  {
    printf("FAIL v3_version_mismatch: should have rejected wrong version\n");
    failures++;
  }
}

static void check_v1_type_mismatch(void)
{
  TelemetryGpsStatus status = {0};
  uint16_t sequence = 0;
  uint8_t bad_packet[TELEMETRY_PACKET_SIZE_V1];
  bool ok;

  memcpy(bad_packet, REF_V1_PACKET_FIX, TELEMETRY_PACKET_SIZE_V1);
  bad_packet[1] = 0xFF; /* wrong type */

  ok = Telemetry_DecodeGpsStatus(bad_packet, TELEMETRY_PACKET_SIZE_V1, &status, &sequence);
  if (ok)
  {
    printf("FAIL v1_type_mismatch: should have rejected wrong type\n");
    failures++;
  }
}

static void check_v3_type_mismatch(void)
{
  TelemetryGpsStatus gps_status = {0};
  TelemetryMpuStatus mpu_status = {0};
  uint16_t sequence = 0;
  uint8_t bad_packet[TELEMETRY_PACKET_SIZE];
  bool ok;

  memcpy(bad_packet, REF_V3_PACKET_FIX, TELEMETRY_PACKET_SIZE);
  bad_packet[1] = 0xFF; /* wrong type */

  ok = Telemetry_DecodeCombinedStatus(bad_packet, TELEMETRY_PACKET_SIZE, &gps_status, &mpu_status, &sequence);
  if (ok)
  {
    printf("FAIL v3_type_mismatch: should have rejected wrong type\n");
    failures++;
  }
}

static void check_v1_short_buffer(void)
{
  TelemetryGpsStatus status = {0};
  uint16_t sequence = 0;
  bool ok;

  ok = Telemetry_DecodeGpsStatus(REF_V1_PACKET_FIX, TELEMETRY_PACKET_SIZE_V1 - 1, &status, &sequence);
  if (ok)
  {
    printf("FAIL v1_short_buffer: should have rejected short buffer\n");
    failures++;
  }

  ok = Telemetry_EncodeGpsStatus(&status, 1, NULL, 0);
  if (ok)
  {
    printf("FAIL v1_short_buffer: should have rejected NULL buffer\n");
    failures++;
  }

  uint8_t tiny[8];
  ok = Telemetry_EncodeGpsStatus(&status, 1, tiny, sizeof(tiny));
  if (ok)
  {
    printf("FAIL v1_short_buffer: should have rejected too-small buffer\n");
    failures++;
  }
}

static void check_v3_short_buffer(void)
{
  TelemetryGpsStatus gps_status = {0};
  TelemetryMpuStatus mpu_status = {0};
  uint16_t sequence = 0;
  bool ok;

  ok = Telemetry_DecodeCombinedStatus(REF_V3_PACKET_FIX, TELEMETRY_PACKET_SIZE - 1, &gps_status, &mpu_status, &sequence);
  if (ok)
  {
    printf("FAIL v3_short_buffer: should have rejected short buffer\n");
    failures++;
  }

  ok = Telemetry_EncodeCombinedStatus(&gps_status, &mpu_status, 1, NULL, 0);
  if (ok)
  {
    printf("FAIL v3_short_buffer: should have rejected NULL buffer\n");
    failures++;
  }

  uint8_t tiny[8];
  ok = Telemetry_EncodeCombinedStatus(&gps_status, &mpu_status, 1, tiny, sizeof(tiny));
  if (ok)
  {
    printf("FAIL v3_short_buffer: should have rejected too-small buffer\n");
    failures++;
  }
}

static void check_v1_bounds_latitude(void)
{
  TelemetryGpsStatus status = {0};
  uint8_t buffer[TELEMETRY_PACKET_SIZE_V1];
  bool ok;

  status.fix_valid = 1;
  status.latitude_udeg = 90000001; /* just over 90 degrees */
  status.longitude_udeg = 0;
  status.altitude_cm = 0;
  status.satellites_used = 1;

  ok = Telemetry_EncodeGpsStatus(&status, 1, buffer, sizeof(buffer));
  if (ok)
  {
    printf("FAIL v1_bounds_latitude: should have rejected latitude > 90 deg\n");
    failures++;
  }

  status.latitude_udeg = -90000001;
  ok = Telemetry_EncodeGpsStatus(&status, 1, buffer, sizeof(buffer));
  if (ok)
  {
    printf("FAIL v1_bounds_latitude: should have rejected latitude < -90 deg\n");
    failures++;
  }
}

static void check_v3_bounds_heading(void)
{
  TelemetryGpsStatus gps_status = {0};
  TelemetryMpuStatus mpu_status = {0};
  uint8_t buffer[TELEMETRY_PACKET_SIZE];
  bool ok;

  gps_status.fix_valid = 0;
  mpu_status.valid = 1;
  mpu_status.heading_valid = 1;
  mpu_status.heading = 5761; /* just over 360*16 */
  mpu_status.roll = 0;
  mpu_status.pitch = 0;
  mpu_status.model = TELEMETRY_IMU_MODEL_MPU9250;

  ok = Telemetry_EncodeCombinedStatus(&gps_status, &mpu_status, 1, buffer, sizeof(buffer));
  if (ok)
  {
    printf("FAIL v3_bounds_heading: should have rejected heading > 360 deg\n");
    failures++;
  }

  mpu_status.heading = -1; /* negative not allowed for heading */
  ok = Telemetry_EncodeCombinedStatus(&gps_status, &mpu_status, 1, buffer, sizeof(buffer));
  if (ok)
  {
    printf("FAIL v3_bounds_heading: should have rejected negative heading\n");
    failures++;
  }

  /* heading must be 0 when heading_valid is 0 */
  mpu_status.heading_valid = 0;
  mpu_status.heading = 100; /* non-zero when heading invalid */
  ok = Telemetry_EncodeCombinedStatus(&gps_status, &mpu_status, 1, buffer, sizeof(buffer));
  if (ok)
  {
    printf("FAIL v3_bounds_heading: should have rejected non-zero heading when heading_valid=0\n");
    failures++;
  }
}

static void check_v3_bounds_roll(void)
{
  TelemetryGpsStatus gps_status = {0};
  TelemetryMpuStatus mpu_status = {0};
  uint8_t buffer[TELEMETRY_PACKET_SIZE];
  bool ok;

  gps_status.fix_valid = 0;
  mpu_status.valid = 1;
  mpu_status.heading_valid = 0;
  mpu_status.heading = 0;
  mpu_status.roll = 2881; /* just over 180*16 */
  mpu_status.pitch = 0;
  mpu_status.model = TELEMETRY_IMU_MODEL_MPU6500;

  ok = Telemetry_EncodeCombinedStatus(&gps_status, &mpu_status, 1, buffer, sizeof(buffer));
  if (ok)
  {
    printf("FAIL v3_bounds_roll: should have rejected roll > 180 deg\n");
    failures++;
  }

  mpu_status.roll = -2881; /* just under -180*16 */
  ok = Telemetry_EncodeCombinedStatus(&gps_status, &mpu_status, 1, buffer, sizeof(buffer));
  if (ok)
  {
    printf("FAIL v3_bounds_roll: should have rejected roll < -180 deg\n");
    failures++;
  }
}

static void check_v3_bounds_pitch(void)
{
  TelemetryGpsStatus gps_status = {0};
  TelemetryMpuStatus mpu_status = {0};
  uint8_t buffer[TELEMETRY_PACKET_SIZE];
  bool ok;

  gps_status.fix_valid = 0;
  mpu_status.valid = 1;
  mpu_status.heading_valid = 0;
  mpu_status.heading = 0;
  mpu_status.roll = 0;
  mpu_status.pitch = 1441; /* just over 90*16 */
  mpu_status.model = TELEMETRY_IMU_MODEL_MPU6500;

  ok = Telemetry_EncodeCombinedStatus(&gps_status, &mpu_status, 1, buffer, sizeof(buffer));
  if (ok)
  {
    printf("FAIL v3_bounds_pitch: should have rejected pitch > 90 deg\n");
    failures++;
  }

  mpu_status.pitch = -1441; /* just under -90*16 */
  ok = Telemetry_EncodeCombinedStatus(&gps_status, &mpu_status, 1, buffer, sizeof(buffer));
  if (ok)
  {
    printf("FAIL v3_bounds_pitch: should have rejected pitch < -90 deg\n");
    failures++;
  }
}

static void check_v1_bounds_longitude(void)
{
  TelemetryGpsStatus status = {0};
  uint8_t buffer[TELEMETRY_PACKET_SIZE_V1];
  bool ok;

  status.fix_valid = 1;
  status.latitude_udeg = 0;
  status.longitude_udeg = 180000001; /* just over 180 degrees */
  status.altitude_cm = 0;
  status.satellites_used = 1;

  ok = Telemetry_EncodeGpsStatus(&status, 1, buffer, sizeof(buffer));
  if (ok)
  {
    printf("FAIL v1_bounds_longitude: should have rejected longitude > 180 deg\n");
    failures++;
  }

  status.longitude_udeg = -180000001;
  ok = Telemetry_EncodeGpsStatus(&status, 1, buffer, sizeof(buffer));
  if (ok)
  {
    printf("FAIL v1_bounds_longitude: should have rejected longitude < -180 deg\n");
    failures++;
  }
}

static void check_v1_bounds_altitude(void)
{
  TelemetryGpsStatus status = {0};
  uint8_t buffer[TELEMETRY_PACKET_SIZE_V1];
  bool ok;

  status.fix_valid = 1;
  status.latitude_udeg = 0;
  status.longitude_udeg = 0;
  status.altitude_cm = 5000001; /* over 50000 m */
  status.satellites_used = 1;

  ok = Telemetry_EncodeGpsStatus(&status, 1, buffer, sizeof(buffer));
  if (ok)
  {
    printf("FAIL v1_bounds_altitude: should have rejected altitude > 50000 m\n");
    failures++;
  }

  status.altitude_cm = -100001; /* under -1000 m */
  ok = Telemetry_EncodeGpsStatus(&status, 1, buffer, sizeof(buffer));
  if (ok)
  {
    printf("FAIL v1_bounds_altitude: should have rejected altitude < -1000 m\n");
    failures++;
  }
}

static void check_v1_bounds_satellites(void)
{
  TelemetryGpsStatus status = {0};
  uint8_t buffer[TELEMETRY_PACKET_SIZE_V1];
  bool ok;

  status.fix_valid = 1;
  status.latitude_udeg = 0;
  status.longitude_udeg = 0;
  status.altitude_cm = 0;
  status.satellites_used = 255; /* max allowed */

  ok = Telemetry_EncodeGpsStatus(&status, 1, buffer, sizeof(buffer));
  if (!ok)
  {
    printf("FAIL v1_bounds_satellites: should have accepted 255 satellites\n");
    failures++;
  }
}

static void check_v1_decode_bounds(void)
{
  TelemetryGpsStatus status = {0};
  uint16_t sequence = 0;
  uint8_t bad_packet[TELEMETRY_PACKET_SIZE_V1];
  bool ok;

  /* Latitude out of bounds in packet: corrupt byte 8 (MSB of latitude) to 0x06
     to make latitude = 0x06DE3634 = 115,000,000 > 90,000,000 */
  memcpy(bad_packet, REF_V1_PACKET_FIX, TELEMETRY_PACKET_SIZE_V1);
  bad_packet[8] = 0x06; /* corrupt latitude MSB */
  ok = Telemetry_DecodeGpsStatus(bad_packet, TELEMETRY_PACKET_SIZE_V1, &status, &sequence);
  if (ok)
  {
    printf("FAIL v1_decode_bounds: should have rejected out-of-bounds latitude\n");
    failures++;
  }

  /* Non-zero coords when fix_valid=0 */
  memcpy(bad_packet, REF_V1_PACKET_NO_FIX, TELEMETRY_PACKET_SIZE_V1);
  bad_packet[4] = 0x01; /* fix_valid = 1 but coords still zero - this is valid */
  ok = Telemetry_DecodeGpsStatus(bad_packet, TELEMETRY_PACKET_SIZE_V1, &status, &sequence);
  if (!ok)
  {
    printf("FAIL v1_decode_bounds: should accept fix_valid=1 with zero coords\n");
    failures++;
  }

  /* fix_valid=0 but non-zero coords */
  memcpy(bad_packet, REF_V1_PACKET_NO_FIX, TELEMETRY_PACKET_SIZE_V1);
  bad_packet[5] = 0x01; /* non-zero latitude */
  ok = Telemetry_DecodeGpsStatus(bad_packet, TELEMETRY_PACKET_SIZE_V1, &status, &sequence);
  if (ok)
  {
    printf("FAIL v1_decode_bounds: should reject fix_valid=0 with non-zero coords\n");
    failures++;
  }
}

static void check_v3_decode_bounds(void)
{
  TelemetryGpsStatus gps_status = {0};
  TelemetryMpuStatus mpu_status = {0};
  uint16_t sequence = 0;
  uint8_t bad_packet[TELEMETRY_PACKET_SIZE];
  bool ok;

  /* GPS latitude out of bounds */
  memcpy(bad_packet, REF_V3_PACKET_FIX, TELEMETRY_PACKET_SIZE);
  bad_packet[8] = 0x06; /* corrupt latitude MSB (byte 8 = latitude[3]) */
  ok = Telemetry_DecodeCombinedStatus(bad_packet, TELEMETRY_PACKET_SIZE, &gps_status, &mpu_status, &sequence);
  if (ok)
  {
    printf("FAIL v3_decode_bounds: should have rejected out-of-bounds latitude\n");
    failures++;
  }

  /* MPU heading out of bounds: heading at bytes 19(LSB), 20(MSB) */
  memcpy(bad_packet, REF_V3_PACKET_FIX, TELEMETRY_PACKET_SIZE);
  bad_packet[20] = 0x17; /* heading MSB = 0x17 -> 0x17D8 = 6104 > 5760 */
  ok = Telemetry_DecodeCombinedStatus(bad_packet, TELEMETRY_PACKET_SIZE, &gps_status, &mpu_status, &sequence);
  if (ok)
  {
    printf("FAIL v3_decode_bounds: should have rejected out-of-bounds heading\n");
    failures++;
  }

  /* MPU roll out of bounds: roll at bytes 21(LSB), 22(MSB) */
  memcpy(bad_packet, REF_V3_PACKET_FIX, TELEMETRY_PACKET_SIZE);
  bad_packet[22] = 0x0C; /* roll MSB = 0x0C -> 0x0CBC = 3260 > 2880 */
  ok = Telemetry_DecodeCombinedStatus(bad_packet, TELEMETRY_PACKET_SIZE, &gps_status, &mpu_status, &sequence);
  if (ok)
  {
    printf("FAIL v3_decode_bounds: should have rejected out-of-bounds roll\n");
    failures++;
  }

  /* MPU pitch out of bounds: pitch at bytes 23(LSB), 24(MSB) */
  memcpy(bad_packet, REF_V3_PACKET_FIX, TELEMETRY_PACKET_SIZE);
  bad_packet[24] = 0x06; /* pitch MSB = 0x06 -> 0x06CC = 1740 > 1440 */
  ok = Telemetry_DecodeCombinedStatus(bad_packet, TELEMETRY_PACKET_SIZE, &gps_status, &mpu_status, &sequence);
  if (ok)
  {
    printf("FAIL v3_decode_bounds: should have rejected out-of-bounds pitch\n");
    failures++;
  }

  /* fix_valid=0 but non-zero GPS coords */
  memcpy(bad_packet, REF_V3_PACKET_NONE, TELEMETRY_PACKET_SIZE);
  bad_packet[5] = 0x01; /* non-zero latitude */
  ok = Telemetry_DecodeCombinedStatus(bad_packet, TELEMETRY_PACKET_SIZE, &gps_status, &mpu_status, &sequence);
  if (ok)
  {
    printf("FAIL v3_decode_bounds: should reject fix_valid=0 with non-zero GPS coords\n");
    failures++;
  }

  /* orient_valid=0 but non-zero MPU fields */
  memcpy(bad_packet, REF_V3_PACKET_NONE, TELEMETRY_PACKET_SIZE);
  bad_packet[19] = 0x01; /* non-zero heading LSB */
  ok = Telemetry_DecodeCombinedStatus(bad_packet, TELEMETRY_PACKET_SIZE, &gps_status, &mpu_status, &sequence);
  if (ok)
  {
    printf("FAIL v3_decode_bounds: should reject orient_valid=0 with non-zero MPU fields\n");
    failures++;
  }

  /* heading_valid=1 but orient_valid=0 (invalid combination) */
  memcpy(bad_packet, REF_V3_PACKET_NONE, TELEMETRY_PACKET_SIZE);
  bad_packet[18] = 0x02; /* heading_valid bit set but orient_valid clear */
  ok = Telemetry_DecodeCombinedStatus(bad_packet, TELEMETRY_PACKET_SIZE, &gps_status, &mpu_status, &sequence);
  if (ok)
  {
    printf("FAIL v3_decode_bounds: should reject heading_valid without orient_valid\n");
    failures++;
  }

  /* non-zero heading when heading_valid=0 */
  memcpy(bad_packet, REF_V3_PACKET_MPU_ONLY, TELEMETRY_PACKET_SIZE);
  bad_packet[19] = 0x10; /* heading = 0x10 = 16 (non-zero) */
  ok = Telemetry_DecodeCombinedStatus(bad_packet, TELEMETRY_PACKET_SIZE, &gps_status, &mpu_status, &sequence);
  if (ok)
  {
    printf("FAIL v3_decode_bounds: should reject non-zero heading when heading_valid=0\n");
    failures++;
  }
}

/* A telemetry packet with trailing bytes is not this layout and must not be
   accepted as a new sample (same strict length rule as the ACK). */
static void check_v1_decode_extra_bytes(void)
{
  TelemetryGpsStatus status = {0};
  uint16_t sequence = 0;
  uint8_t extra_packet[TELEMETRY_PACKET_SIZE_V1 + 1];
  bool ok;

  memcpy(extra_packet, REF_V1_PACKET_FIX, TELEMETRY_PACKET_SIZE_V1);
  extra_packet[TELEMETRY_PACKET_SIZE_V1] = 0x00; /* extra byte */

  ok = Telemetry_DecodeGpsStatus(extra_packet, sizeof(extra_packet), &status, &sequence);
  if (ok)
  {
    printf("FAIL v1_decode_extra_bytes: should have rejected packet with extra bytes\n");
    failures++;
  }

  ok = Telemetry_DecodeGpsStatus(REF_V1_PACKET_FIX, sizeof(REF_V1_PACKET_FIX), &status, &sequence);
  if (!ok)
  {
    printf("FAIL v1_decode_extra_bytes: rejected the exact-length reference packet\n");
    failures++;
  }
}

static void check_v3_decode_extra_bytes(void)
{
  TelemetryGpsStatus gps_status = {0};
  TelemetryMpuStatus mpu_status = {0};
  uint16_t sequence = 0;
  uint8_t extra_packet[TELEMETRY_PACKET_SIZE + 1];
  bool ok;

  memcpy(extra_packet, REF_V3_PACKET_FIX, TELEMETRY_PACKET_SIZE);
  extra_packet[TELEMETRY_PACKET_SIZE] = 0x00; /* extra byte */

  ok = Telemetry_DecodeCombinedStatus(extra_packet, sizeof(extra_packet), &gps_status, &mpu_status, &sequence);
  if (ok)
  {
    printf("FAIL v3_decode_extra_bytes: should have rejected packet with extra bytes\n");
    failures++;
  }

  ok = Telemetry_DecodeCombinedStatus(REF_V3_PACKET_FIX, sizeof(REF_V3_PACKET_FIX), &gps_status, &mpu_status, &sequence);
  if (!ok)
  {
    printf("FAIL v3_decode_extra_bytes: rejected the exact-length reference packet\n");
    failures++;
  }
}

static void check_is_fix_current(void)
{
  TelemetryGpsStatus status = {0};
  bool ok;

  status.fix_valid = 1;
  status.fix_timestamp_ms = 1000;

  /* Fix is current (age 0) */
  ok = Telemetry_IsFixCurrent(&status, 1000);
  if (!ok)
  {
    printf("FAIL is_fix_current: age 0 should be current\n");
    failures++;
  }

  /* Fix is current (age 1000) */
  ok = Telemetry_IsFixCurrent(&status, 2000);
  if (!ok)
  {
    printf("FAIL is_fix_current: age 1000 should be current\n");
    failures++;
  }

  /* Fix is stale (age 30001 > 30000) */
  ok = Telemetry_IsFixCurrent(&status, 31001);
  if (ok)
  {
    printf("FAIL is_fix_current: age 30001 should be stale\n");
    failures++;
  }

  /* Fix not valid */
  status.fix_valid = 0;
  ok = Telemetry_IsFixCurrent(&status, 1000);
  if (ok)
  {
    printf("FAIL is_fix_current: fix_valid=0 should be false\n");
    failures++;
  }

  /* Wrap-around: fix timestamp near wrap, now past wrap, but fix is stale.
     Fix at 0xFFFFF000 (4096ms before wrap), now at 0x11000 (69632ms after wrap).
     Real elapsed = 69632 + 4096 = 73728ms > 30000. */
  status.fix_valid = 1;
  status.fix_timestamp_ms = 0xFFFFF000u;
  ok = Telemetry_IsFixCurrent(&status, 0x11000u);
  if (ok)
  {
    printf("FAIL is_fix_current: wrap-around stale fix should be rejected\n");
    failures++;
  }

  /* Wrap-around: fix timestamp near wrap, now past wrap, fix is current.
     Fix at 0xFFFFF000 (4096ms before wrap), now at 0x2000 (8192ms after wrap).
     Real elapsed = 8192 + 4096 = 12288ms < 30000. */
  status.fix_timestamp_ms = 0xFFFFF000u;
  ok = Telemetry_IsFixCurrent(&status, 0x2000u);
  if (!ok)
  {
    printf("FAIL is_fix_current: wrap-around current fix should be accepted\n");
    failures++;
  }
}

static void check_invalidate_fix(void)
{
  TelemetryGpsStatus status = {0};

  status.fix_valid = 1;
  status.latitude_udeg = 12345;
  Telemetry_InvalidateFix(&status);
  if (status.fix_valid != 0)
  {
    printf("FAIL invalidate_fix: fix_valid should be 0\n");
    failures++;
  }

  Telemetry_InvalidateFix(NULL); /* should not crash */
}

static void check_encode_ack(void)
{
  uint8_t buffer[TELEMETRY_ACK_PACKET_SIZE];
  bool ok;

  ok = Telemetry_EncodeAck(42, buffer, sizeof(buffer));
  if (!ok)
  {
    printf("FAIL encode_ack: Telemetry_EncodeAck returned false\n");
    failures++;
    return;
  }
  if (memcmp(buffer, REF_ACK_PACKET, TELEMETRY_ACK_PACKET_SIZE) != 0)
  {
    printf("FAIL encode_ack: packet mismatch\n");
    for (uint16_t i = 0; i < TELEMETRY_ACK_PACKET_SIZE; i++)
    {
      if (buffer[i] != REF_ACK_PACKET[i])
      {
        printf("  byte %u: got 0x%02X, expected 0x%02X\n", i, buffer[i], REF_ACK_PACKET[i]);
      }
    }
    failures++;
    return;
  }
}

static void check_decode_ack(void)
{
  uint16_t sequence = 0;
  bool ok;

  ok = Telemetry_DecodeAck(REF_ACK_PACKET, sizeof(REF_ACK_PACKET), &sequence);
  if (!ok)
  {
    printf("FAIL decode_ack: Telemetry_DecodeAck returned false\n");
    failures++;
    return;
  }
  if (sequence != 42)
  {
    printf("FAIL decode_ack: sequence %u, expected 42\n", sequence);
    failures++;
  }
}

static void check_ack_version_mismatch(void)
{
  uint16_t sequence = 0;
  uint8_t bad_packet[TELEMETRY_ACK_PACKET_SIZE];
  bool ok;

  memcpy(bad_packet, REF_ACK_PACKET, TELEMETRY_ACK_PACKET_SIZE);
  bad_packet[0] = 0xFF; /* wrong version */

  ok = Telemetry_DecodeAck(bad_packet, TELEMETRY_ACK_PACKET_SIZE, &sequence);
  if (ok)
  {
    printf("FAIL ack_version_mismatch: should have rejected wrong version\n");
    failures++;
  }
}

static void check_ack_type_mismatch(void)
{
  uint16_t sequence = 0;
  uint8_t bad_packet[TELEMETRY_ACK_PACKET_SIZE];
  bool ok;

  memcpy(bad_packet, REF_ACK_PACKET, TELEMETRY_ACK_PACKET_SIZE);
  bad_packet[1] = 0xFF; /* wrong type */

  ok = Telemetry_DecodeAck(bad_packet, TELEMETRY_ACK_PACKET_SIZE, &sequence);
  if (ok)
  {
    printf("FAIL ack_type_mismatch: should have rejected wrong type\n");
    failures++;
  }
}

static void check_ack_short_buffer(void)
{
  uint16_t sequence = 0;
  bool ok;

  ok = Telemetry_DecodeAck(REF_ACK_PACKET, TELEMETRY_ACK_PACKET_SIZE - 1, &sequence);
  if (ok)
  {
    printf("FAIL ack_short_buffer: should have rejected short buffer\n");
    failures++;
  }

  ok = Telemetry_EncodeAck(1, NULL, 0);
  if (ok)
  {
    printf("FAIL ack_short_buffer: should have rejected NULL buffer\n");
    failures++;
  }

  uint8_t tiny[2];
  ok = Telemetry_EncodeAck(1, tiny, sizeof(tiny));
  if (ok)
  {
    printf("FAIL ack_short_buffer: should have rejected too-small buffer\n");
    failures++;
  }
}

static void check_ack_extra_bytes(void)
{
  uint16_t sequence = 0;
  uint8_t extra_packet[TELEMETRY_ACK_PACKET_SIZE + 1];
  bool ok;

  memcpy(extra_packet, REF_ACK_PACKET, TELEMETRY_ACK_PACKET_SIZE);
  extra_packet[TELEMETRY_ACK_PACKET_SIZE] = 0x00; /* extra byte */

  ok = Telemetry_DecodeAck(extra_packet, sizeof(extra_packet), &sequence);
  if (ok)
  {
    printf("FAIL ack_extra_bytes: should have rejected packet with extra bytes\n");
    failures++;
  }
}

static void check_telemetry_not_mistaken_as_ack(void)
{
  uint16_t sequence = 0;
  bool ok;

  /* A valid v1 telemetry packet must never be accepted as an ACK */
  ok = Telemetry_DecodeAck(REF_V1_PACKET_FIX, TELEMETRY_PACKET_SIZE_V1, &sequence);
  if (ok)
  {
    printf("FAIL v1_telemetry_not_ack: telemetry packet was accepted as ACK\n");
    failures++;
  }

  ok = Telemetry_DecodeAck(REF_V1_PACKET_NO_FIX, TELEMETRY_PACKET_SIZE_V1, &sequence);
  if (ok)
  {
    printf("FAIL v1_telemetry_not_ack: no-fix telemetry packet was accepted as ACK\n");
    failures++;
  }

  /* A valid v3 telemetry packet must never be accepted as an ACK */
  ok = Telemetry_DecodeAck(REF_V3_PACKET_FIX, TELEMETRY_PACKET_SIZE, &sequence);
  if (ok)
  {
    printf("FAIL v3_telemetry_not_ack: telemetry packet was accepted as ACK\n");
    failures++;
  }

  ok = Telemetry_DecodeAck(REF_V3_PACKET_NONE, TELEMETRY_PACKET_SIZE, &sequence);
  if (ok)
  {
    printf("FAIL v3_telemetry_not_ack: no-fix telemetry packet was accepted as ACK\n");
    failures++;
  }
}

static void check_ack_not_mistaken_as_telemetry(void)
{
  TelemetryGpsStatus status = {0};
  uint16_t sequence = 0;
  bool ok;

  /* An ACK packet must never be accepted as a v1 telemetry packet */
  ok = Telemetry_DecodeGpsStatus(REF_ACK_PACKET, TELEMETRY_ACK_PACKET_SIZE, &status, &sequence);
  if (ok)
  {
    printf("FAIL ack_not_v1_telemetry: ACK packet was accepted as v1 telemetry\n");
    failures++;
  }

  TelemetryGpsStatus gps_status = {0};
  TelemetryMpuStatus mpu_status = {0};
  /* An ACK packet must never be accepted as a v3 telemetry packet */
  ok = Telemetry_DecodeCombinedStatus(REF_ACK_PACKET, TELEMETRY_ACK_PACKET_SIZE, &gps_status, &mpu_status, &sequence);
  if (ok)
  {
    printf("FAIL ack_not_v3_telemetry: ACK packet was accepted as v3 telemetry\n");
    failures++;
  }
}

static void check_signed_mpu_fields(void)
{
  /* Verify that signed heading/roll/pitch survive encode/decode round-trip */
  TelemetryGpsStatus gps_status = {0};
  TelemetryMpuStatus mpu_status = {0}, decoded_mpu = {0};
  TelemetryGpsStatus decoded_gps = {0};
  uint8_t buffer[TELEMETRY_PACKET_SIZE];
  uint16_t sequence = 0;
  bool ok;

  gps_status.fix_valid = 0;
  mpu_status.valid = 1;
  mpu_status.heading_valid = 1;
  mpu_status.heading = 0;       /* 0 deg */
  mpu_status.roll = -2880;      /* -180 deg */
  mpu_status.pitch = 1440;      /* +90 deg */
  mpu_status.model = TELEMETRY_IMU_MODEL_MPU9250;

  ok = Telemetry_EncodeCombinedStatus(&gps_status, &mpu_status, 1, buffer, sizeof(buffer));
  if (!ok)
  {
    printf("FAIL signed_mpu_fields: encode failed\n");
    failures++;
    return;
  }

  ok = Telemetry_DecodeCombinedStatus(buffer, TELEMETRY_PACKET_SIZE, &decoded_gps, &decoded_mpu, &sequence);
  if (!ok)
  {
    printf("FAIL signed_mpu_fields: decode failed\n");
    failures++;
    return;
  }

  if (decoded_mpu.heading != 0 || decoded_mpu.roll != -2880 || decoded_mpu.pitch != 1440)
  {
    printf("FAIL signed_mpu_fields: values mismatch (hdg=%d, roll=%d, pitch=%d)\n",
           decoded_mpu.heading, decoded_mpu.roll, decoded_mpu.pitch);
    failures++;
  }
}

static void check_mpu6500_no_heading(void)
{
  /* MPU-6500 must not have heading_valid set, and heading must be 0 */
  TelemetryGpsStatus gps_status = {0};
  TelemetryMpuStatus mpu_status = {0};
  uint8_t buffer[TELEMETRY_PACKET_SIZE];
  bool ok;

  gps_status.fix_valid = 0;
  mpu_status.valid = 1;
  mpu_status.heading_valid = 0;
  mpu_status.model = TELEMETRY_IMU_MODEL_MPU6500;
  mpu_status.heading = 0;
  mpu_status.roll = -68;
  mpu_status.pitch = 204;

  ok = Telemetry_EncodeCombinedStatus(&gps_status, &mpu_status, 1, buffer, sizeof(buffer));
  if (!ok)
  {
    printf("FAIL mpu6500_no_heading: encode failed\n");
    failures++;
    return;
  }

  /* Verify heading is 0 in the encoded packet */
  if (buffer[19] != 0 || buffer[20] != 0)
  {
    printf("FAIL mpu6500_no_heading: heading not zero in packet\n");
    failures++;
  }

  /* Verify model byte */
  if (buffer[25] != TELEMETRY_IMU_MODEL_MPU6500)
  {
    printf("FAIL mpu6500_no_heading: model byte not 1\n");
    failures++;
  }

  /* Decode and verify */
  TelemetryGpsStatus decoded_gps = {0};
  TelemetryMpuStatus decoded_mpu = {0};
  uint16_t sequence = 0;
  ok = Telemetry_DecodeCombinedStatus(buffer, TELEMETRY_PACKET_SIZE, &decoded_gps, &decoded_mpu, &sequence);
  if (!ok)
  {
    printf("FAIL mpu6500_no_heading: decode failed\n");
    failures++;
    return;
  }
  if (decoded_mpu.heading_valid != 0 || decoded_mpu.heading != 0)
  {
    printf("FAIL mpu6500_no_heading: decoded heading_valid=%u heading=%d\n",
           decoded_mpu.heading_valid, decoded_mpu.heading);
    failures++;
  }
  if (decoded_mpu.model != TELEMETRY_IMU_MODEL_MPU6500)
  {
    printf("FAIL mpu6500_no_heading: decoded model=%u\n", decoded_mpu.model);
    failures++;
  }
}

int main(void)
{
  /* v1 tests */
  check_v1_encode_fix();
  check_v1_encode_no_fix();
  check_v1_decode_fix();
  check_v1_decode_no_fix();
  check_v1_version_mismatch();
  check_v1_type_mismatch();
  check_v1_short_buffer();
  check_v1_bounds_latitude();
  check_v1_bounds_longitude();
  check_v1_bounds_altitude();
  check_v1_bounds_satellites();
  check_v1_decode_bounds();
  check_v1_decode_extra_bytes();

  /* v3 tests */
  check_v3_encode_combined_fix();
  check_v3_encode_gps_only();
  check_v3_encode_mpu_only();
  check_v3_encode_none();
  check_v3_decode_combined_fix();
  check_v3_decode_gps_only();
  check_v3_decode_mpu_only();
  check_v3_decode_none();
  check_v3_version_mismatch();
  check_v3_type_mismatch();
  check_v3_short_buffer();
  check_v3_bounds_heading();
  check_v3_bounds_roll();
  check_v3_bounds_pitch();
  check_v3_decode_bounds();
  check_v3_decode_extra_bytes();
  check_signed_mpu_fields();
  check_mpu6500_no_heading();

  /* common tests */
  check_is_fix_current();
  check_invalidate_fix();
  check_encode_ack();
  check_decode_ack();
  check_ack_version_mismatch();
  check_ack_type_mismatch();
  check_ack_short_buffer();
  check_ack_extra_bytes();
  check_telemetry_not_mistaken_as_ack();
  check_ack_not_mistaken_as_telemetry();

  if (failures != 0u)
  {
    printf("%u failure(s)\n", failures);
    return 1;
  }

  printf("telemetry: all tests passed\n");
  return 0;
}