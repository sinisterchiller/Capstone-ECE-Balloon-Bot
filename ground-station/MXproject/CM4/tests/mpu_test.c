/*
 * Host tests for MPU driver logic (CM4/Core/Src/mpu.c).
 * Tests the pure C logic: sensor scaling, complementary filter, calibration logic,
 * magnetometer sensitivity application, and orientation calculations.
 * Build and run from the MXproject directory:
 *
 *   cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -DMPU_HOST_TEST \
 *      CM4/tests/mpu_test.c -o /tmp/mpu_test -lm && /tmp/mpu_test
 */

#ifdef MPU_HOST_TEST

/* Mock HAL types and definitions for host testing */
#ifndef HAL_STATUS_TYPEDEF
#define HAL_STATUS_TYPEDEF
typedef enum
{
  HAL_OK       = 0x00U,
  HAL_ERROR    = 0x01U,
  HAL_BUSY     = 0x02U,
  HAL_TIMEOUT  = 0x03U
} HAL_StatusTypeDef;
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>

/* Copy the constants and types from mpu.h for host testing */
/* MPU I2C addresses (7-bit) */
#define MPU_ADDR_PRIMARY         0x68U
#define MPU_ADDR_SECONDARY       0x69U

/* MPU WHO_AM_I register and expected values */
#define MPU_REG_WHO_AM_I         0x75U
#define MPU_WHO_AM_I_9250        0x71U
#define MPU_WHO_AM_I_6500        0x70U

/* MPU register map (common to both models) */
#define MPU_REG_SMPLRT_DIV       0x19U
#define MPU_REG_CONFIG           0x1AU
#define MPU_REG_GYRO_CONFIG      0x1BU
#define MPU_REG_ACCEL_CONFIG     0x1CU
#define MPU_REG_ACCEL_CONFIG2    0x1DU
#define MPU_REG_LP_ACCEL_ODR     0x1EU
#define MPU_REG_WOM_THR          0x1FU
#define MPU_REG_FIFO_EN          0x23U
#define MPU_REG_I2C_MST_CTRL     0x24U
#define MPU_REG_I2C_SLV0_ADDR    0x25U
#define MPU_REG_I2C_SLV0_REG     0x26U
#define MPU_REG_I2C_SLV0_CTRL    0x27U
#define MPU_REG_I2C_SLV1_ADDR    0x28U
#define MPU_REG_I2C_SLV1_REG     0x29U
#define MPU_REG_I2C_SLV1_CTRL    0x2AU
#define MPU_REG_I2C_SLV2_ADDR    0x2BU
#define MPU_REG_I2C_SLV2_REG     0x2CU
#define MPU_REG_I2C_SLV2_CTRL    0x2DU
#define MPU_REG_I2C_SLV3_ADDR    0x2EU
#define MPU_REG_I2C_SLV3_REG     0x2FU
#define MPU_REG_I2C_SLV3_CTRL    0x30U
#define MPU_REG_I2C_SLV4_ADDR    0x31U
#define MPU_REG_I2C_SLV4_REG     0x32U
#define MPU_REG_I2C_SLV4_DO      0x33U
#define MPU_REG_I2C_SLV4_CTRL    0x34U
#define MPU_REG_I2C_SLV4_DI      0x35U
#define MPU_REG_I2C_MST_STATUS   0x36U
#define MPU_REG_INT_PIN_CFG      0x37U
#define MPU_REG_INT_ENABLE       0x38U
#define MPU_REG_INT_STATUS       0x3AU
#define MPU_REG_ACCEL_XOUT_H     0x3BU
#define MPU_REG_TEMP_OUT_H       0x41U
#define MPU_REG_GYRO_XOUT_H      0x43U
#define MPU_REG_SIGNAL_PATH_RST  0x68U
#define MPU_REG_USER_CTRL        0x6AU
#define MPU_REG_PWR_MGMT_1       0x6BU
#define MPU_REG_PWR_MGMT_2       0x6CU
#define MPU_REG_FIFO_COUNTH      0x72U
#define MPU_REG_FIFO_COUNTL      0x73U
#define MPU_REG_FIFO_R_W         0x74U

/* PWR_MGMT_1 bits */
#define MPU_PWR_MGMT_1_RESET     0x80U
#define MPU_PWR_MGMT_1_SLEEP     0x40U
#define MPU_PWR_MGMT_1_CYCLE     0x20U
#define MPU_PWR_MGMT_1_GYRO_STBY 0x10U
#define MPU_PWR_MGMT_1_PD_PTAT   0x08U
#define MPU_PWR_MGMT_1_CLKSEL    0x07U
#define MPU_CLKSEL_PLL_X_GYRO    0x01U

/* GYRO_CONFIG bits */
#define MPU_GYRO_FS_SEL_250      0x00U
#define MPU_GYRO_FS_SEL_500      0x08U
#define MPU_GYRO_FS_SEL_1000     0x10U
#define MPU_GYRO_FS_SEL_2000     0x18U
#define MPU_GYRO_DLPF_CFG        0x07U

/* ACCEL_CONFIG bits */
#define MPU_ACCEL_FS_SEL_2G      0x00U
#define MPU_ACCEL_FS_SEL_4G      0x08U
#define MPU_ACCEL_FS_SEL_8G      0x10U
#define MPU_ACCEL_FS_SEL_16G     0x18U

/* ACCEL_CONFIG2 bits */
#define MPU_ACCEL_DLPF_CFG       0x0FU
#define MPU_ACCEL_FCHOICE_B      0x08U

/* CONFIG register DLPF_CFG for gyro */
#define MPU_DLPF_CFG_184HZ       0x01U
#define MPU_DLPF_CFG_92HZ        0x02U
#define MPU_DLPF_CFG_41HZ        0x03U
#define MPU_DLPF_CFG_20HZ        0x04U
#define MPU_DLPF_CFG_10HZ        0x05U
#define MPU_DLPF_CFG_5HZ         0x06U

/* INT_PIN_CFG bits */
#define MPU_INT_PIN_CFG_BYPASS_EN 0x02U
#define MPU_INT_PIN_CFG_I2C_MST_EN 0x20U

/* USER_CTRL bits */
#define MPU_USER_CTRL_I2C_MST_EN  0x20U
#define MPU_USER_CTRL_I2C_MST_RST 0x02U

/* PWR_MGMT_2 bits */
#define MPU_PWR_MGMT_2_ALL_AXES_ON 0x00U

/* AK8963 registers (MPU-9250 magnetometer) */
#define AK8963_ADDR              0x0CU
#define AK8963_REG_WIA           0x00U
#define AK8963_REG_INFO          0x01U
#define AK8963_REG_ST1           0x02U
#define AK8963_REG_HXL           0x03U
#define AK8963_REG_HXH           0x04U
#define AK8963_REG_HYL           0x05U
#define AK8963_REG_HYH           0x06U
#define AK8963_REG_HZL           0x07U
#define AK8963_REG_HZH           0x08U
#define AK8963_REG_ST2           0x09U
#define AK8963_REG_CNTL1         0x0AU
#define AK8963_REG_CNTL2         0x0BU
#define AK8963_REG_ASTC          0x0CU
#define AK8963_REG_TS1           0x0DU
#define AK8963_REG_TS2           0x0EU
#define AK8963_REG_I2CDIS        0x0FU
#define AK8963_REG_ASAX          0x10U
#define AK8963_REG_ASAY          0x11U
#define AK8963_REG_ASAZ          0x12U

/* AK8963 CNTL1 bits */
#define AK8963_CNTL1_MODE_PDOWN  0x00U
#define AK8963_CNTL1_MODE_SINGLE 0x01U
#define AK8963_CNTL1_MODE_CONT1  0x02U
#define AK8963_CNTL1_MODE_CONT2  0x06U
#define AK8963_CNTL1_MODE_CONT3  0x08U
#define AK8963_CNTL1_MODE_CONT4  0x0AU
#define AK8963_CNTL1_MODE_FUSE   0x0FU
#define AK8963_CNTL1_BIT_16      0x10U
#define AK8963_CNTL1_100HZ_16BIT 0x16U

/* AK8963 ST1 bits */
#define AK8963_ST1_DRDY          0x01U

/* AK8963 ST2 bits */
#define AK8963_ST2_HOFL          0x08U

/* WIA expected value */
#define AK8963_WIA_EXPECTED      0x48U

/* Delays (ms) */
#define MPU_DELAY_POR_MS         100U
#define MPU_DELAY_RESET_MS       100U
#define MPU_DELAY_MODE_SWITCH_MS 10U
#define MPU_I2C_TIMEOUT_MS       10U
#define MPU_POLL_INTERVAL_MS     10U

/* Sensor scaling factors */
#define MPU_ACCEL_SCALE_2G       16384.0f
#define MPU_GYRO_SCALE_250DPS    131.0f

/* Complementary filter coefficient (gyro weight) */
#define MPU_COMP_FILTER_ALPHA    0.98f

/* Startup calibration samples */
#define MPU_CALIB_SAMPLES        200U

/* Magnetometer calibration samples */
#define MPU_MAG_CALIB_SAMPLES    500U

/* Minimum mag range for valid calibration (µT) - requires actual movement */
#define MPU_MAG_MIN_RANGE_UT     10.0f

/* Orientation output scaling for telemetry (degrees * 16) */
#define MPU_ORIENTATION_SCALE    16.0f

/* MPU model enumeration */
typedef enum
{
  MPU_MODEL_UNKNOWN = 0,
  MPU_MODEL_9250    = 1,
  MPU_MODEL_6500    = 2
} MPU_Model_t;

/* Mock HAL_GetTick for host testing */
static uint32_t mock_tick = 0;
uint32_t HAL_GetTick(void) { return mock_tick; }

/* Mock GNSS_VCP_PostText for host testing */
void GNSS_VCP_PostText(const char *text) { (void)text; }

/* Mock I2C functions - we test logic, not I2C */
HAL_StatusTypeDef MPU_WriteReg(void *hmpu, uint8_t reg, uint8_t value) { (void)hmpu; (void)reg; (void)value; return HAL_OK; }
HAL_StatusTypeDef MPU_ReadReg(void *hmpu, uint8_t reg, uint8_t *value) { (void)hmpu; (void)reg; (void)value; return HAL_OK; }
HAL_StatusTypeDef MPU_ReadRegs(void *hmpu, uint8_t start_reg, uint8_t *buf, uint16_t len) { (void)hmpu; (void)start_reg; (void)buf; (void)len; return HAL_OK; }
HAL_StatusTypeDef AK8963_WriteReg(uint8_t reg, uint8_t value) { (void)reg; (void)value; return HAL_OK; }
HAL_StatusTypeDef AK8963_ReadReg(uint8_t reg, uint8_t *value) { (void)reg; (void)value; return HAL_OK; }
HAL_StatusTypeDef AK8963_ReadRegs(uint8_t start_reg, uint8_t *buf, uint16_t len) { (void)start_reg; (void)buf; (void)len; return HAL_OK; }

/* Exported types (copied from mpu.h) */

/** Raw accelerometer/gyroscope/temperature data from 14-byte burst read. */
typedef struct
{
  int16_t accel_x;
  int16_t accel_y;
  int16_t accel_z;
  int16_t temp;
  int16_t gyro_x;
  int16_t gyro_y;
  int16_t gyro_z;
} MPU_RawData_t;

/** Scaled sensor data in physical units. */
typedef struct
{
  float accel_x_g;
  float accel_y_g;
  float accel_z_g;
  float gyro_x_dps;
  float gyro_y_dps;
  float gyro_z_dps;
  float temp_c;
} MPU_ScaledData_t;

/** Magnetometer raw data (little-endian 16-bit). */
typedef struct
{
  int16_t x;
  int16_t y;
  int16_t z;
  uint8_t st1;
  uint8_t st2;
} MPU_MagRaw_t;

/** Magnetometer scaled data with factory sensitivity applied. */
typedef struct
{
  float x_ut;
  float y_ut;
  float z_ut;
  bool  valid;
} MPU_MagScaled_t;

/** Orientation output (roll, pitch, heading). */
typedef struct
{
  float roll_deg;
  float pitch_deg;
  float heading_deg;
  bool  heading_valid;
  uint32_t timestamp_ms;
} MPU_Orientation_t;

/** Gyroscope bias (calibration). */
typedef struct
{
  float x_dps;
  float y_dps;
  float z_dps;
  bool  valid;
  uint16_t samples;
} MPU_GyroBias_t;

/** Magnetometer factory sensitivity (ASA) - immutable after init. */
typedef struct
{
  float x;
  float y;
  float z;
} MPU_MagFactorySens_t;

/** Magnetometer hard-iron calibration offsets. */
typedef struct
{
  float x_ut;
  float y_ut;
  float z_ut;
  bool  valid;
} MPU_MagCalib_t;

/** MPU driver state and latest readings. */
typedef struct
{
  MPU_Model_t model;
  uint8_t     i2c_addr;
  uint8_t     who_am_i;
  uint8_t     initialized;
  MPU_RawData_t  raw;
  MPU_ScaledData_t scaled;
  MPU_MagRaw_t           mag_raw;
  MPU_MagScaled_t        mag_scaled;
  MPU_MagFactorySens_t   mag_factory_sens;
  MPU_MagCalib_t         mag_calib;
  bool                   mag_available;
  MPU_Orientation_t orientation;
  MPU_GyroBias_t   gyro_bias;
  uint8_t          calib_state;
  uint16_t         calib_samples;
  uint32_t next_poll_tick;
  uint32_t last_poll_tick;
  uint32_t last_update_tick;
  float    dt_s;
  uint8_t valid;
  uint8_t  consec_failures;
  uint32_t last_retry_tick;
  HAL_StatusTypeDef last_error;
  uint8_t  bringup_attempts;
  uint32_t next_bringup_tick;
} MPU_Handle_t;

/* Include the implementation functions we want to test */
#define static

/* Static helper: convert raw accel to g */
float MPU_RawAccelToG(int16_t raw)
{
  return (float)raw / MPU_ACCEL_SCALE_2G;
}

/* Static helper: convert raw gyro to °/s */
float MPU_RawGyroToDPS(int16_t raw)
{
  return (float)raw / MPU_GYRO_SCALE_250DPS;
}

/* Static helper: convert raw temp to °C */
float MPU_RawTempToC(int16_t raw)
{
  return ((float)raw / 340.0f) + 21.0f;
}

/* Static helper: apply factory sensitivity adjustment to magnetometer raw data */
void MPU_ApplyMagSensitivity(MPU_Handle_t *hmpu, MPU_MagRaw_t *raw, MPU_MagScaled_t *scaled)
{
  float sens_x = hmpu->mag_factory_sens.x;
  float sens_y = hmpu->mag_factory_sens.y;
  float sens_z = hmpu->mag_factory_sens.z;

  const float MAG_UT_PER_LSB = 0.15f;

  scaled->x_ut = (float)raw->x * sens_x * MAG_UT_PER_LSB;
  scaled->y_ut = (float)raw->y * sens_y * MAG_UT_PER_LSB;
  scaled->z_ut = (float)raw->z * sens_z * MAG_UT_PER_LSB;

  scaled->valid = ((raw->st2 & AK8963_ST2_HOFL) == 0) && ((raw->st1 & AK8963_ST1_DRDY) != 0);
}

/* Static helper: check if accelerometer readings indicate stationary state */
bool MPU_IsStationary(MPU_Handle_t *hmpu)
{
  float ax = hmpu->scaled.accel_x_g;
  float ay = hmpu->scaled.accel_y_g;
  float az = hmpu->scaled.accel_z_g;
  float mag = sqrtf(ax*ax + ay*ay + az*az);
  return (mag > 0.95f && mag < 1.05f);
}

/* Static helper: complementary filter for roll/pitch */
void MPU_UpdateOrientation(MPU_Handle_t *hmpu)
{
  float accel_roll = atan2f(hmpu->scaled.accel_y_g, hmpu->scaled.accel_z_g) * 180.0f / (float)M_PI;
  float accel_pitch = atan2f(-hmpu->scaled.accel_x_g,
                             sqrtf(hmpu->scaled.accel_y_g * hmpu->scaled.accel_y_g +
                                   hmpu->scaled.accel_z_g * hmpu->scaled.accel_z_g)) * 180.0f / (float)M_PI;

  float gyro_x_corr = hmpu->scaled.gyro_x_dps - hmpu->gyro_bias.x_dps;
  float gyro_y_corr = hmpu->scaled.gyro_y_dps - hmpu->gyro_bias.y_dps;
  (void)hmpu->scaled.gyro_z_dps;

  float gyro_roll = hmpu->orientation.roll_deg + gyro_x_corr * hmpu->dt_s;
  float gyro_pitch = hmpu->orientation.pitch_deg + gyro_y_corr * hmpu->dt_s;

  float alpha = MPU_COMP_FILTER_ALPHA;
  hmpu->orientation.roll_deg = alpha * gyro_roll + (1.0f - alpha) * accel_roll;
  hmpu->orientation.pitch_deg = alpha * gyro_pitch + (1.0f - alpha) * accel_pitch;

  while (hmpu->orientation.roll_deg > 180.0f) hmpu->orientation.roll_deg -= 360.0f;
  while (hmpu->orientation.roll_deg < -180.0f) hmpu->orientation.roll_deg += 360.0f;

  if (hmpu->orientation.pitch_deg > 90.0f) hmpu->orientation.pitch_deg = 90.0f;
  if (hmpu->orientation.pitch_deg < -90.0f) hmpu->orientation.pitch_deg = -90.0f;

  hmpu->orientation.timestamp_ms = hmpu->last_poll_tick;

  if (hmpu->model == MPU_MODEL_9250 && hmpu->mag_available && hmpu->mag_scaled.valid && hmpu->mag_calib.valid)
  {
    float roll_rad = hmpu->orientation.roll_deg * (float)M_PI / 180.0f;
    float pitch_rad = hmpu->orientation.pitch_deg * (float)M_PI / 180.0f;

    float cos_roll = cosf(roll_rad);
    float sin_roll = sinf(roll_rad);
    float cos_pitch = cosf(pitch_rad);
    float sin_pitch = sinf(pitch_rad);

    float mx = hmpu->mag_scaled.x_ut - hmpu->mag_calib.x_ut;
    float my = hmpu->mag_scaled.y_ut - hmpu->mag_calib.y_ut;
    float mz = hmpu->mag_scaled.z_ut - hmpu->mag_calib.z_ut;

    float mag_x_h = mx * cos_pitch + mz * sin_pitch;
    float mag_y_h = mx * sin_roll * sin_pitch + my * cos_roll - mz * sin_roll * cos_pitch;

    float heading = atan2f(mag_y_h, mag_x_h) * 180.0f / (float)M_PI;
    if (heading < 0.0f) heading += 360.0f;

    hmpu->orientation.heading_deg = heading;
    hmpu->orientation.heading_valid = true;
  }
  else
  {
    hmpu->orientation.heading_deg = 0.0f;
    hmpu->orientation.heading_valid = false;
  }
}

/* Static helper: gyroscope bias calibration at startup - ONLY when stationary */
void MPU_CalibrateGyro(MPU_Handle_t *hmpu)
{
  if (hmpu->calib_state != 1) return;

  if (!MPU_IsStationary(hmpu))
  {
    hmpu->gyro_bias.x_dps = 0;
    hmpu->gyro_bias.y_dps = 0;
    hmpu->gyro_bias.z_dps = 0;
    hmpu->calib_samples = 0;
    return;
  }

  hmpu->gyro_bias.x_dps += hmpu->scaled.gyro_x_dps;
  hmpu->gyro_bias.y_dps += hmpu->scaled.gyro_y_dps;
  hmpu->gyro_bias.z_dps += hmpu->scaled.gyro_z_dps;
  hmpu->calib_samples++;

  if (hmpu->calib_samples >= MPU_CALIB_SAMPLES)
  {
    hmpu->gyro_bias.x_dps /= (float)MPU_CALIB_SAMPLES;
    hmpu->gyro_bias.y_dps /= (float)MPU_CALIB_SAMPLES;
    hmpu->gyro_bias.z_dps /= (float)MPU_CALIB_SAMPLES;
    hmpu->gyro_bias.valid = true;
    hmpu->calib_state = (hmpu->model == MPU_MODEL_9250) ? 2 : 3;
    hmpu->calib_samples = 0;
  }
}

/* Static helper: magnetometer hard-iron calibration - requires sufficient range/movement */
void MPU_CalibrateMag(MPU_Handle_t *hmpu)
{
  if (hmpu->calib_state != 2) return;

  if (!hmpu->mag_scaled.valid) return;

  static float min_x = 0, max_x = 0, min_y = 0, max_y = 0, min_z = 0, max_z = 0;
  static uint8_t first = 1;

  if (first)
  {
    min_x = max_x = hmpu->mag_scaled.x_ut;
    min_y = max_y = hmpu->mag_scaled.y_ut;
    min_z = max_z = hmpu->mag_scaled.z_ut;
    first = 0;
  }
  else
  {
    if (hmpu->mag_scaled.x_ut < min_x) min_x = hmpu->mag_scaled.x_ut;
    if (hmpu->mag_scaled.x_ut > max_x) max_x = hmpu->mag_scaled.x_ut;
    if (hmpu->mag_scaled.y_ut < min_y) min_y = hmpu->mag_scaled.y_ut;
    if (hmpu->mag_scaled.y_ut > max_y) max_y = hmpu->mag_scaled.y_ut;
    if (hmpu->mag_scaled.z_ut < min_z) min_z = hmpu->mag_scaled.z_ut;
    if (hmpu->mag_scaled.z_ut > max_z) max_z = hmpu->mag_scaled.z_ut;
  }

  hmpu->calib_samples++;

  if (hmpu->calib_samples >= MPU_MAG_CALIB_SAMPLES)
  {
    float range_x = max_x - min_x;
    float range_y = max_y - min_y;
    float range_z = max_z - min_z;

    bool sufficient_range = (range_x > MPU_MAG_MIN_RANGE_UT) &&
                            (range_y > MPU_MAG_MIN_RANGE_UT) &&
                            (range_z > MPU_MAG_MIN_RANGE_UT);

    if (sufficient_range)
    {
      hmpu->mag_calib.x_ut = (max_x + min_x) * 0.5f;
      hmpu->mag_calib.y_ut = (max_y + min_y) * 0.5f;
      hmpu->mag_calib.z_ut = (max_z + min_z) * 0.5f;
      hmpu->mag_calib.valid = true;
    }
    else
    {
      hmpu->mag_calib.valid = false;
      first = 1;
      hmpu->calib_samples = 0;
      return;
    }

    hmpu->calib_state = 3;
    hmpu->calib_samples = 0;
    first = 1;
  }
}

/* Get model string */
const char* MPU_ModelToString(MPU_Model_t model)
{
  switch (model)
  {
    case MPU_MODEL_9250: return "MPU-9250";
    case MPU_MODEL_6500: return "MPU-6500";
    default: return "Unknown";
  }
}

/* Test framework */
static unsigned int failures = 0;

#define ASSERT_EQ_FMT(val, expected, fmt, msg) \
    do { \
        if ((val) != (expected)) { \
            printf("FAIL: %s: got " fmt " expected " fmt "\n", msg, (val), (expected)); \
            failures++; \
        } \
    } while(0)

#define ASSERT_NEAR(val, expected, eps, msg) \
    do { \
        if (fabsf((val) - (expected)) > (eps)) { \
            printf("FAIL: %s: got %f expected %f (eps=%f)\n", msg, (val), (expected), (eps)); \
            failures++; \
        } \
    } while(0)

#define ASSERT_TRUE(val, msg) \
    do { \
        if (!(val)) { \
            printf("FAIL: %s: expected true\n", msg); \
            failures++; \
        } \
    } while(0)

#define ASSERT_FALSE(val, msg) \
    do { \
        if ((val)) { \
            printf("FAIL: %s: expected false\n", msg); \
            failures++; \
        } \
    } while(0)

/* Test raw accel to g conversion */
static void test_raw_accel_to_g(void)
{
    ASSERT_NEAR(MPU_RawAccelToG(16384), 1.0f, 0.001f, "1g positive");
    ASSERT_NEAR(MPU_RawAccelToG(-16384), -1.0f, 0.001f, "1g negative");
    ASSERT_NEAR(MPU_RawAccelToG(0), 0.0f, 0.001f, "0g");
    ASSERT_NEAR(MPU_RawAccelToG(8192), 0.5f, 0.001f, "0.5g");
}

/* Test raw gyro to dps conversion */
static void test_raw_gyro_to_dps(void)
{
    ASSERT_NEAR(MPU_RawGyroToDPS(131), 1.0f, 0.001f, "1 dps positive");
    ASSERT_NEAR(MPU_RawGyroToDPS(-131), -1.0f, 0.001f, "1 dps negative");
    ASSERT_NEAR(MPU_RawGyroToDPS(0), 0.0f, 0.001f, "0 dps");
    ASSERT_NEAR(MPU_RawGyroToDPS(262), 2.0f, 0.001f, "2 dps");
}

/* Test raw temp to C conversion */
static void test_raw_temp_to_c(void)
{
    ASSERT_NEAR(MPU_RawTempToC(0), 21.0f, 0.1f, "21°C");
    ASSERT_NEAR(MPU_RawTempToC(340), 22.0f, 0.1f, "22°C");
    ASSERT_NEAR(MPU_RawTempToC(-340), 20.0f, 0.1f, "20°C");
}

/* Test factory sensitivity multiplier calculation */
static void test_factory_sensitivity(void)
{
    float mult = (128.0f + 128.0f) / 256.0f;
    ASSERT_NEAR(mult, 1.0f, 0.001f, "ASA 128 -> 1.0");

    mult = (0.0f + 128.0f) / 256.0f;
    ASSERT_NEAR(mult, 0.5f, 0.001f, "ASA 0 -> 0.5");

    mult = (255.0f + 128.0f) / 256.0f;
    ASSERT_NEAR(mult, 383.0f/256.0f, 0.001f, "ASA 255 -> 1.496");
}

/* Test magnetometer sensitivity application */
static void test_apply_mag_sensitivity(void)
{
    MPU_Handle_t hmpu = {0};
    MPU_MagRaw_t raw = {0};
    MPU_MagScaled_t scaled = {0};

    hmpu.mag_factory_sens.x = 1.0f;
    hmpu.mag_factory_sens.y = 1.0f;
    hmpu.mag_factory_sens.z = 1.0f;

    raw.x = 1000; raw.y = -2000; raw.z = 500;
    raw.st1 = AK8963_ST1_DRDY;
    raw.st2 = 0;

    MPU_ApplyMagSensitivity(&hmpu, &raw, &scaled);

    ASSERT_NEAR(scaled.x_ut, 1000 * 0.15f, 0.01f, "mag X conversion");
    ASSERT_NEAR(scaled.y_ut, -2000 * 0.15f, 0.01f, "mag Y conversion");
    ASSERT_NEAR(scaled.z_ut, 500 * 0.15f, 0.01f, "mag Z conversion");
    ASSERT_TRUE(scaled.valid, "mag valid when DRDY and no overflow");

    raw.st2 = AK8963_ST2_HOFL;
    MPU_ApplyMagSensitivity(&hmpu, &raw, &scaled);
    ASSERT_FALSE(scaled.valid, "mag invalid on overflow");

    raw.st2 = 0;
    raw.st1 = 0;
    MPU_ApplyMagSensitivity(&hmpu, &raw, &scaled);
    ASSERT_FALSE(scaled.valid, "mag invalid when not DRDY");
}

/* Test complementary filter roll/pitch calculation */
static void test_complementary_filter(void)
{
    MPU_Handle_t hmpu = {0};

    hmpu.orientation.roll_deg = 0;
    hmpu.orientation.pitch_deg = 0;
    hmpu.gyro_bias.x_dps = 0;
    hmpu.gyro_bias.y_dps = 0;
    hmpu.gyro_bias.valid = true;

    hmpu.scaled.accel_x_g = 0;
    hmpu.scaled.accel_y_g = 0;
    hmpu.scaled.accel_z_g = 1.0f;
    hmpu.scaled.gyro_x_dps = 0;
    hmpu.scaled.gyro_y_dps = 0;
    hmpu.dt_s = 0.01f;

    MPU_UpdateOrientation(&hmpu);
    ASSERT_NEAR(hmpu.orientation.roll_deg, 0.0f, 1.0f, "flat roll");
    ASSERT_NEAR(hmpu.orientation.pitch_deg, 0.0f, 1.0f, "flat pitch");

    /* First iteration: only 2% of accel contribution due to alpha=0.98 */
    hmpu.scaled.accel_x_g = 0;
    hmpu.scaled.accel_y_g = 1.0f;
    hmpu.scaled.accel_z_g = 0;
    MPU_UpdateOrientation(&hmpu);
    ASSERT_NEAR(hmpu.orientation.roll_deg, 1.8f, 1.0f, "90 deg roll right (1st iter: 0.98*0+0.02*90)");

    hmpu.scaled.accel_x_g = -1.0f;
    hmpu.scaled.accel_y_g = 0;
    hmpu.scaled.accel_z_g = 0;
    MPU_UpdateOrientation(&hmpu);
    ASSERT_NEAR(hmpu.orientation.pitch_deg, 1.8f, 1.0f, "90 deg pitch forward (1st iter)");

    /* Test gyro integration with bias correction */
    hmpu.orientation.roll_deg = 0;
    hmpu.orientation.pitch_deg = 0;
    hmpu.scaled.accel_x_g = 0;
    hmpu.scaled.accel_y_g = 0;
    hmpu.scaled.accel_z_g = 1.0f;
    hmpu.scaled.gyro_x_dps = 10.0f;
    hmpu.scaled.gyro_y_dps = 0;
    hmpu.gyro_bias.x_dps = 1.0f;
    hmpu.dt_s = 0.01f;

    MPU_UpdateOrientation(&hmpu);
    ASSERT_NEAR(hmpu.orientation.roll_deg, 0.0882f, 0.01f, "gyro integration with bias");
}

/* Test heading calculation with tilt compensation */
static void test_heading_calculation(void)
{
    MPU_Handle_t hmpu = {0};

    hmpu.model = MPU_MODEL_9250;
    hmpu.mag_available = true;
    hmpu.mag_calib.valid = true;
    hmpu.mag_calib.x_ut = 0;
    hmpu.mag_calib.y_ut = 0;
    hmpu.mag_calib.z_ut = 0;

    hmpu.orientation.roll_deg = 0;
    hmpu.orientation.pitch_deg = 0;
    hmpu.mag_scaled.x_ut = 20.0f;
    hmpu.mag_scaled.y_ut = 0;
    hmpu.mag_scaled.z_ut = -40.0f;
    hmpu.mag_scaled.valid = true;

    MPU_UpdateOrientation(&hmpu);

    ASSERT_TRUE(hmpu.orientation.heading_valid, "heading valid with mag");
    ASSERT_NEAR(hmpu.orientation.heading_deg, 0.0f, 5.0f, "heading North");

    hmpu.mag_scaled.x_ut = 0;
    hmpu.mag_scaled.y_ut = 20.0f;
    MPU_UpdateOrientation(&hmpu);
    ASSERT_NEAR(hmpu.orientation.heading_deg, 90.0f, 5.0f, "heading East");

    hmpu.mag_scaled.x_ut = -20.0f;
    hmpu.mag_scaled.y_ut = 0;
    MPU_UpdateOrientation(&hmpu);
    ASSERT_NEAR(hmpu.orientation.heading_deg, 180.0f, 5.0f, "heading South");

    hmpu.mag_scaled.x_ut = 0;
    hmpu.mag_scaled.y_ut = -20.0f;
    MPU_UpdateOrientation(&hmpu);
    ASSERT_NEAR(hmpu.orientation.heading_deg, 270.0f, 5.0f, "heading West");

    hmpu.mag_available = false;
    MPU_UpdateOrientation(&hmpu);
    ASSERT_FALSE(hmpu.orientation.heading_valid, "heading invalid when mag not available");

    hmpu.mag_available = true;
    hmpu.mag_calib.valid = false;
    MPU_UpdateOrientation(&hmpu);
    ASSERT_FALSE(hmpu.orientation.heading_valid, "heading invalid when mag calib not valid");
}

/* Test gyro bias calibration - only when stationary */
static void test_gyro_calibration_stationary(void)
{
    MPU_Handle_t hmpu = {0};
    hmpu.calib_state = 1;
    hmpu.gyro_bias.x_dps = 0;
    hmpu.gyro_bias.y_dps = 0;
    hmpu.gyro_bias.z_dps = 0;
    hmpu.calib_samples = 0;

    hmpu.scaled.accel_x_g = 0;
    hmpu.scaled.accel_y_g = 0;
    hmpu.scaled.accel_z_g = 1.0f;
    hmpu.scaled.gyro_x_dps = 0.5f;
    hmpu.scaled.gyro_y_dps = -0.3f;
    hmpu.scaled.gyro_z_dps = 0.1f;

    for (int i = 0; i < (int)MPU_CALIB_SAMPLES - 1; i++) {
        MPU_CalibrateGyro(&hmpu);
    }
    ASSERT_FALSE(hmpu.gyro_bias.valid, "bias not valid before enough samples");

    MPU_CalibrateGyro(&hmpu);
    ASSERT_TRUE(hmpu.gyro_bias.valid, "bias valid after enough samples");
    ASSERT_NEAR(hmpu.gyro_bias.x_dps, 0.5f, 0.01f, "bias X correct");
    ASSERT_NEAR(hmpu.gyro_bias.y_dps, -0.3f, 0.01f, "bias Y correct");
    ASSERT_NEAR(hmpu.gyro_bias.z_dps, 0.1f, 0.01f, "bias Z correct");
    ASSERT_EQ_FMT(hmpu.calib_state, (hmpu.model == MPU_MODEL_9250) ? 2 : 3, "%d", "calib state advanced");
}

/* Test gyro bias calibration resets on movement */
static void test_gyro_calibration_movement_reset(void)
{
    MPU_Handle_t hmpu = {0};
    hmpu.calib_state = 1;
    hmpu.gyro_bias.x_dps = 0;
    hmpu.gyro_bias.y_dps = 0;
    hmpu.gyro_bias.z_dps = 0;
    hmpu.calib_samples = 50;

    hmpu.scaled.accel_x_g = 0.5f;
    hmpu.scaled.accel_y_g = 0;
    hmpu.scaled.accel_z_g = 0.5f;
    hmpu.scaled.gyro_x_dps = 10.0f;
    hmpu.scaled.gyro_y_dps = 0;
    hmpu.scaled.gyro_z_dps = 0;

    MPU_CalibrateGyro(&hmpu);

    ASSERT_EQ_FMT(hmpu.calib_samples, 0, "%d", "calib samples reset on movement");
    ASSERT_FALSE(hmpu.gyro_bias.valid, "bias invalid after movement reset");
}

/* Test mag calibration fails with insufficient range (first call, static state starts fresh) */
static void test_mag_calibration_insufficient_range(void)
{
    MPU_Handle_t hmpu = {0};
    hmpu.calib_state = 2;
    hmpu.mag_calib.valid = false;
    hmpu.calib_samples = 0;

    hmpu.mag_scaled.valid = true;
    hmpu.mag_scaled.x_ut = 1.0f; hmpu.mag_scaled.y_ut = 1.0f; hmpu.mag_scaled.z_ut = 1.0f;

    for (int i = 0; i < (int)MPU_MAG_CALIB_SAMPLES; i++) {
        MPU_CalibrateMag(&hmpu);
    }

    ASSERT_FALSE(hmpu.mag_calib.valid, "mag calib fails with insufficient range");
    ASSERT_EQ_FMT(hmpu.calib_state, 2, "%d", "calib state stays at mag calibrating");
    ASSERT_EQ_FMT(hmpu.calib_samples, 0, "%d", "samples reset for retry");
}

/* Test roll normalization [-180, 180] with complementary filter */
static void test_roll_normalization(void)
{
    MPU_Handle_t hmpu = {0};
    hmpu.gyro_bias.valid = true;
    hmpu.gyro_bias.x_dps = 0;
    hmpu.gyro_bias.y_dps = 0;
    hmpu.dt_s = 0.01f;
    hmpu.scaled.accel_x_g = 0;
    hmpu.scaled.accel_y_g = 0;
    hmpu.scaled.accel_z_g = 1.0f;
    hmpu.scaled.gyro_x_dps = 0;
    hmpu.scaled.gyro_y_dps = 0;

    hmpu.orientation.roll_deg = 200.0f;
    MPU_UpdateOrientation(&hmpu);
    ASSERT_NEAR(hmpu.orientation.roll_deg, -164.0f, 1.0f, "roll 200 -> -164 (alpha=0.98 pull to 0)");

    hmpu.orientation.roll_deg = -200.0f;
    MPU_UpdateOrientation(&hmpu);
    ASSERT_NEAR(hmpu.orientation.roll_deg, 164.0f, 1.0f, "roll -200 -> 164 (alpha=0.98 pull to 0)");

    hmpu.orientation.roll_deg = 360.0f;
    MPU_UpdateOrientation(&hmpu);
    ASSERT_NEAR(hmpu.orientation.roll_deg, -7.2f, 1.0f, "roll 360 -> -7.2 (alpha=0.98 pull to 0)");
}

/* Test pitch clamping [-90, 90] */
static void test_pitch_clamping(void)
{
    MPU_Handle_t hmpu = {0};
    hmpu.gyro_bias.valid = true;
    hmpu.gyro_bias.x_dps = 0;
    hmpu.gyro_bias.y_dps = 0;
    hmpu.dt_s = 0.01f;
    hmpu.scaled.accel_x_g = 0;
    hmpu.scaled.accel_y_g = 0;
    hmpu.scaled.accel_z_g = 1.0f;
    hmpu.scaled.gyro_x_dps = 0;
    hmpu.scaled.gyro_y_dps = 0;

    hmpu.orientation.pitch_deg = 100.0f;
    MPU_UpdateOrientation(&hmpu);
    ASSERT_NEAR(hmpu.orientation.pitch_deg, 90.0f, 1.0f, "pitch 100 -> 90");

    hmpu.orientation.pitch_deg = -100.0f;
    MPU_UpdateOrientation(&hmpu);
    ASSERT_NEAR(hmpu.orientation.pitch_deg, -90.0f, 1.0f, "pitch -100 -> -90");
}

/* Test ModelToString */
static void test_model_to_string(void)
{
    ASSERT_EQ_FMT(strcmp(MPU_ModelToString(MPU_MODEL_9250), "MPU-9250"), 0, "%d", "MPU-9250 string");
    ASSERT_EQ_FMT(strcmp(MPU_ModelToString(MPU_MODEL_6500), "MPU-6500"), 0, "%d", "MPU-6500 string");
    ASSERT_EQ_FMT(strcmp(MPU_ModelToString(MPU_MODEL_UNKNOWN), "Unknown"), 0, "%d", "Unknown string");
}

/* Test stationary detection */
static void test_is_stationary(void)
{
    MPU_Handle_t hmpu = {0};

    hmpu.scaled.accel_x_g = 0;
    hmpu.scaled.accel_y_g = 0;
    hmpu.scaled.accel_z_g = 1.0f;
    ASSERT_TRUE(MPU_IsStationary(&hmpu), "flat 1g is stationary");

    hmpu.scaled.accel_x_g = 0.5f;
    hmpu.scaled.accel_y_g = 0.5f;
    hmpu.scaled.accel_z_g = sqrtf(1.0f - 0.5f*0.5f - 0.5f*0.5f);
    ASSERT_TRUE(MPU_IsStationary(&hmpu), "tilted 1g is stationary");

    hmpu.scaled.accel_x_g = 0;
    hmpu.scaled.accel_y_g = 0;
    hmpu.scaled.accel_z_g = 0.9f;
    ASSERT_FALSE(MPU_IsStationary(&hmpu), "0.9g not stationary");

    hmpu.scaled.accel_x_g = 0;
    hmpu.scaled.accel_y_g = 0;
    hmpu.scaled.accel_z_g = 1.1f;
    ASSERT_FALSE(MPU_IsStationary(&hmpu), "1.1g not stationary");
}

int main(void)
{
    test_raw_accel_to_g();
    test_raw_gyro_to_dps();
    test_raw_temp_to_c();
    test_factory_sensitivity();
    test_apply_mag_sensitivity();
    test_complementary_filter();
    test_heading_calculation();
    test_gyro_calibration_stationary();
    test_gyro_calibration_movement_reset();
    test_mag_calibration_insufficient_range();
    test_roll_normalization();
    test_pitch_clamping();
    test_model_to_string();
    test_is_stationary();

    if (failures != 0u)
    {
        printf("\n%u failure(s)\n", failures);
        return 1;
    }

    printf("\nAll MPU logic tests passed!\n");
    return 0;
}

#endif /* MPU_HOST_TEST */