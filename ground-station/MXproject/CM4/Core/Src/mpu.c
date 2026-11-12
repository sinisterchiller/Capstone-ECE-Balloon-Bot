/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    mpu.c
  * @brief   MPU-9250 / MPU-6500 IMU driver implementation using STM32 HAL I2C.
  *          Detection, initialization, 100 Hz raw sampling, AK8963 magnetometer
  *          support, and orientation calculation (roll, pitch, magnetic heading).
  ******************************************************************************
  * @attention
  *
  * This software component is licensed under terms that can be found in the
  * LICENSE file in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "mpu.h"
#include "gnss_vcp.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdbool.h>

/* USER CODE BEGIN 0 */

/* Static helper: write single register using HAL_I2C_Mem_Write */
static HAL_StatusTypeDef MPU_WriteReg(MPU_Handle_t *hmpu, uint8_t reg, uint8_t value)
{
  return HAL_I2C_Mem_Write(&hi2c2, (uint16_t)hmpu->i2c_addr << 1, reg,
                           I2C_MEMADD_SIZE_8BIT, &value, 1, MPU_I2C_TIMEOUT_MS);
}

/* Static helper: read single register using HAL_I2C_Mem_Read */
static HAL_StatusTypeDef MPU_ReadReg(MPU_Handle_t *hmpu, uint8_t reg, uint8_t *value)
{
  return HAL_I2C_Mem_Read(&hi2c2, (uint16_t)hmpu->i2c_addr << 1, reg,
                          I2C_MEMADD_SIZE_8BIT, value, 1, MPU_I2C_TIMEOUT_MS);
}

/* Static helper: read multiple sequential registers */
static HAL_StatusTypeDef MPU_ReadRegs(MPU_Handle_t *hmpu, uint8_t start_reg, uint8_t *buf, uint16_t len)
{
  return HAL_I2C_Mem_Read(&hi2c2, (uint16_t)hmpu->i2c_addr << 1, start_reg,
                          I2C_MEMADD_SIZE_8BIT, buf, len, MPU_I2C_TIMEOUT_MS);
}

/* Static helper: write single register to AK8963 via I2C bypass */
static HAL_StatusTypeDef AK8963_WriteReg(uint8_t reg, uint8_t value)
{
  return HAL_I2C_Mem_Write(&hi2c2, (uint16_t)AK8963_ADDR << 1, reg,
                           I2C_MEMADD_SIZE_8BIT, &value, 1, MPU_I2C_TIMEOUT_MS);
}

/* Static helper: read single register from AK8963 via I2C bypass */
static HAL_StatusTypeDef AK8963_ReadReg(uint8_t reg, uint8_t *value)
{
  return HAL_I2C_Mem_Read(&hi2c2, (uint16_t)AK8963_ADDR << 1, reg,
                          I2C_MEMADD_SIZE_8BIT, value, 1, MPU_I2C_TIMEOUT_MS);
}

/* Static helper: read multiple registers from AK8963 via I2C bypass */
static HAL_StatusTypeDef AK8963_ReadRegs(uint8_t start_reg, uint8_t *buf, uint16_t len)
{
  return HAL_I2C_Mem_Read(&hi2c2, (uint16_t)AK8963_ADDR << 1, start_reg,
                          I2C_MEMADD_SIZE_8BIT, buf, len, MPU_I2C_TIMEOUT_MS);
}

/* Static helper: log diagnostic I2C error with HAL status, error code, ISR */
static void MPU_LogI2CError(const char *context, HAL_StatusTypeDef status)
{
  uint32_t hal_error = HAL_I2C_GetError(&hi2c2);
  uint32_t isr = hi2c2.Instance->ISR;
  char buf[128];
  int len = snprintf(buf, sizeof(buf),
                     "MPU %s: status=%d err=0x%08lX ISR=0x%08lX",
                     context, (int)status,
                     (unsigned long)hal_error, (unsigned long)isr);
  if (len > 0 && len < (int)sizeof(buf))
  {
    (void)GNSS_VCP_PostText(buf);
  }
}

/* Static helper: convert raw accel to g */
static inline float MPU_RawAccelToG(int16_t raw)
{
  return (float)raw / MPU_ACCEL_SCALE_2G;
}

/* Static helper: convert raw gyro to °/s */
static inline float MPU_RawGyroToDPS(int16_t raw)
{
  return (float)raw / MPU_GYRO_SCALE_250DPS;
}

/* Static helper: convert raw temp to °C (from datasheet: 340 LSB/°C, 0 at 21°C) */
static inline float MPU_RawTempToC(int16_t raw)
{
  return ((float)raw / 340.0f) + 21.0f;
}

/* Static helper: apply factory sensitivity adjustment to magnetometer raw data */
static void MPU_ApplyMagSensitivity(MPU_Handle_t *hmpu, MPU_MagRaw_t *raw, MPU_MagScaled_t *scaled)
{
  /* Use immutable factory sensitivity multipliers (ASA) */
  float sens_x = hmpu->mag_factory_sens.x;
  float sens_y = hmpu->mag_factory_sens.y;
  float sens_z = hmpu->mag_factory_sens.z;

  /* AK8963 raw data is little-endian. Convert to µT:
   * 16-bit mode: 0.15 µT/LSB = 4900 µT / 32760
   * Full scale is ±4900 µT = ±32760 counts (0x7FF8)
   */
  const float MAG_UT_PER_LSB = 0.15f;

  scaled->x_ut = (float)raw->x * sens_x * MAG_UT_PER_LSB;
  scaled->y_ut = (float)raw->y * sens_y * MAG_UT_PER_LSB;
  scaled->z_ut = (float)raw->z * sens_z * MAG_UT_PER_LSB;

  /* Check overflow and data ready */
  scaled->valid = ((raw->st2 & AK8963_ST2_HOFL) == 0) && ((raw->st1 & AK8963_ST1_DRDY) != 0);
}

/* Static helper: check if sensor is stationary.
 * Returns true only when both accel magnitude is close to 1g AND gyro magnitude is near zero.
 * This prevents bias calibration during active rotation. */
static bool MPU_IsStationary(MPU_Handle_t *hmpu)
{
  /* Stationary if accel magnitude is close to 1g (within ~5%) */
  float ax = hmpu->scaled.accel_x_g;
  float ay = hmpu->scaled.accel_y_g;
  float az = hmpu->scaled.accel_z_g;
  float accel_mag = sqrtf(ax*ax + ay*ay + az*az);

  /* Also check gyro magnitude is small (below ~2 °/s) to avoid calibrating during rotation */
  float gx = hmpu->scaled.gyro_x_dps;
  float gy = hmpu->scaled.gyro_y_dps;
  float gz = hmpu->scaled.gyro_z_dps;
  float gyro_mag = sqrtf(gx*gx + gy*gy + gz*gz);

  return (accel_mag > 0.95f && accel_mag < 1.05f) && (gyro_mag < 2.0f);
}

/* Static helper: complementary filter for roll/pitch */
static void MPU_UpdateOrientation(MPU_Handle_t *hmpu)
{
  /* Accelerometer-derived tilt */
  float accel_roll = atan2f(hmpu->scaled.accel_y_g, hmpu->scaled.accel_z_g) * 180.0f / (float)M_PI;
  float accel_pitch = atan2f(-hmpu->scaled.accel_x_g,
                             sqrtf(hmpu->scaled.accel_y_g * hmpu->scaled.accel_y_g +
                                   hmpu->scaled.accel_z_g * hmpu->scaled.accel_z_g)) * 180.0f / (float)M_PI;

  /* Gyroscope integration with bias correction */
  float gyro_x_corr = hmpu->scaled.gyro_x_dps - hmpu->gyro_bias.x_dps;
  float gyro_y_corr = hmpu->scaled.gyro_y_dps - hmpu->gyro_bias.y_dps;
  (void)hmpu->scaled.gyro_z_dps; /* yaw not used for roll/pitch */

  /* Integrate gyro rates */
  float gyro_roll = hmpu->orientation.roll_deg + gyro_x_corr * hmpu->dt_s;
  float gyro_pitch = hmpu->orientation.pitch_deg + gyro_y_corr * hmpu->dt_s;

  /* Complementary filter */
  float alpha = MPU_COMP_FILTER_ALPHA;
  hmpu->orientation.roll_deg = alpha * gyro_roll + (1.0f - alpha) * accel_roll;
  hmpu->orientation.pitch_deg = alpha * gyro_pitch + (1.0f - alpha) * accel_pitch;

  /* Normalize roll to [-180, 180] */
  while (hmpu->orientation.roll_deg > 180.0f) hmpu->orientation.roll_deg -= 360.0f;
  while (hmpu->orientation.roll_deg < -180.0f) hmpu->orientation.roll_deg += 360.0f;

  /* Clamp pitch to [-90, 90] */
  if (hmpu->orientation.pitch_deg > 90.0f) hmpu->orientation.pitch_deg = 90.0f;
  if (hmpu->orientation.pitch_deg < -90.0f) hmpu->orientation.pitch_deg = -90.0f;

  /* Update timestamp */
  hmpu->orientation.timestamp_ms = hmpu->last_poll_tick;

  /* Heading calculation (MPU-9250 only, requires mag_available, valid calibration,
   * known axis alignment, and soft-iron calibration).
   * The AK8963 magnetometer axes may not align with accel/gyro axes.
   * Axis alignment must be physically calibrated (mag_align.known).
   * Soft-iron scale correction requires calibration (mag_soft_iron.valid).
   * If alignment is unknown or soft-iron uncalibrated, heading_valid remains false. */
  if (hmpu->model == MPU_MODEL_9250 &&
      hmpu->mag_available &&
      hmpu->mag_scaled.valid &&
      hmpu->mag_calib.valid &&
      hmpu->mag_align.known &&
      hmpu->mag_soft_iron.valid)
  {
    /* Tilt-compensated magnetic heading with axis alignment and soft-iron correction */
    float roll_rad = hmpu->orientation.roll_deg * (float)M_PI / 180.0f;
    float pitch_rad = hmpu->orientation.pitch_deg * (float)M_PI / 180.0f;

    float cos_roll = cosf(roll_rad);
    float sin_roll = sinf(roll_rad);
    float cos_pitch = cosf(pitch_rad);
    float sin_pitch = sinf(pitch_rad);

    /* Hard-iron corrected magnetometer */
    float mx_raw = hmpu->mag_scaled.x_ut - hmpu->mag_calib.x_ut;
    float my_raw = hmpu->mag_scaled.y_ut - hmpu->mag_calib.y_ut;
    float mz_raw = hmpu->mag_scaled.z_ut - hmpu->mag_calib.z_ut;

    /* Apply soft-iron scale correction (diagonal) */
    float mx_si = mx_raw * hmpu->mag_soft_iron.x;
    float my_si = my_raw * hmpu->mag_soft_iron.y;
    float mz_si = mz_raw * hmpu->mag_soft_iron.z;

    /* Apply axis alignment matrix (magnetometer -> accel/gyro frame) */
    float mx = hmpu->mag_align.m[0][0] * mx_si + hmpu->mag_align.m[0][1] * my_si + hmpu->mag_align.m[0][2] * mz_si;
    float my = hmpu->mag_align.m[1][0] * mx_si + hmpu->mag_align.m[1][1] * my_si + hmpu->mag_align.m[1][2] * mz_si;
    float mz = hmpu->mag_align.m[2][0] * mx_si + hmpu->mag_align.m[2][1] * my_si + hmpu->mag_align.m[2][2] * mz_si;

    /* Tilt compensation */
    float mag_x_h = mx * cos_pitch + mz * sin_pitch;
    float mag_y_h = mx * sin_roll * sin_pitch + my * cos_roll - mz * sin_roll * cos_pitch;

    /* Heading in degrees, 0..360 */
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
static void MPU_CalibrateGyro(MPU_Handle_t *hmpu)
{
  if (hmpu->calib_state != 1) return;

  /* Only accumulate bias when stationary (both accel and gyro magnitudes checked) */
  if (!MPU_IsStationary(hmpu))
  {
    /* Reset accumulation if movement detected - wait for stable period */
    hmpu->gyro_bias.x_dps = 0;
    hmpu->gyro_bias.y_dps = 0;
    hmpu->gyro_bias.z_dps = 0;
    hmpu->calib_samples = 0;
    return;
  }

  /* Accumulate gyro readings */
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
    hmpu->calib_state = (hmpu->model == MPU_MODEL_9250) ? 2 : 3; /* mag cal or done */
    hmpu->calib_samples = 0;

    char buf[64];
    int len = snprintf(buf, sizeof(buf),
                       "MPU gyro bias: x=%.3f y=%.3f z=%.3f dps",
                       hmpu->gyro_bias.x_dps, hmpu->gyro_bias.y_dps, hmpu->gyro_bias.z_dps);
    if (len > 0 && len < (int)sizeof(buf))
    {
      (void)GNSS_VCP_PostText(buf);
    }
  }
}

/* Static helper: magnetometer hard-iron calibration - requires sufficient range/movement */
static void MPU_CalibrateMag(MPU_Handle_t *hmpu)
{
  if (hmpu->calib_state != 2) return;

  if (!hmpu->mag_scaled.valid) return;

  /* Track min/max for each axis */
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
    /* Check that we have sufficient range on each axis (requires movement) */
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

      char buf[64];
      int len = snprintf(buf, sizeof(buf),
                         "MPU mag calib: x=%.1f y=%.1f z=%.1f uT (range: %.1f/%.1f/%.1f)",
                         hmpu->mag_calib.x_ut, hmpu->mag_calib.y_ut, hmpu->mag_calib.z_ut,
                         range_x, range_y, range_z);
      if (len > 0 && len < (int)sizeof(buf))
      {
        (void)GNSS_VCP_PostText(buf);
      }
    }
    else
    {
      /* Insufficient movement - report and restart calibration */
      hmpu->mag_calib.valid = false;
      char buf[80];
      int len = snprintf(buf, sizeof(buf),
                         "MPU mag calib failed: range x=%.1f y=%.1f z=%.1f need>%.1f",
                         range_x, range_y, range_z, MPU_MAG_MIN_RANGE_UT);
      if (len > 0 && len < (int)sizeof(buf))
      {
        (void)GNSS_VCP_PostText(buf);
      }
      /* Restart calibration */
      first = 1;
      hmpu->calib_samples = 0;
      return;
    }

    hmpu->calib_state = 3; /* done */
    hmpu->calib_samples = 0;
    first = 1; /* reset for potential re-calibration */
  }
}

/* USER CODE END 0 */

/**
  * @brief  Probe for MPU at supported addresses (0x68, 0x69).
  *         Reads WHO_AM_I (0x75) to identify model.
  * @param  hmpu: Pointer to MPU_Handle_t state structure (zeroed by caller).
  * @retval HAL_OK on success (device found and identified),
  *         HAL_ERROR if no supported device at either address,
  *         HAL_TIMEOUT if bus communication failed.
  */
HAL_StatusTypeDef MPU_Probe(MPU_Handle_t *hmpu)
{
  HAL_StatusTypeDef status;
  uint8_t who_am_i;
  uint8_t addr_tried = 0;
  uint16_t addresses[2] = {MPU_I2C_ADDR_PRIMARY, MPU_I2C_ADDR_SECONDARY};

  if (hmpu == NULL)
  {
    return HAL_ERROR;
  }

  for (addr_tried = 0; addr_tried < 2; addr_tried++)
  {
    hmpu->i2c_addr = addresses[addr_tried];

    /* Read WHO_AM_I register */
    status = MPU_ReadReg(hmpu, MPU_REG_WHO_AM_I, &who_am_i);

    /* Capture and log diagnostics - PRESERVE HAL ERROR CODES */
    uint32_t hal_error = HAL_I2C_GetError(&hi2c2);
    uint32_t isr = hi2c2.Instance->ISR;

    char buf[128];
    int len;
    if (status == HAL_OK)
    {
      len = snprintf(buf, sizeof(buf),
                     "MPU probe 0x%02X: status=%d err=0x%08lX ISR=0x%08lX WHO_AM_I=0x%02X",
                     hmpu->i2c_addr, (int)status,
                     (unsigned long)hal_error, (unsigned long)isr, who_am_i);
    }
    else
    {
      len = snprintf(buf, sizeof(buf),
                     "MPU probe 0x%02X: status=%d err=0x%08lX ISR=0x%08lX",
                     hmpu->i2c_addr, (int)status,
                     (unsigned long)hal_error, (unsigned long)isr);
    }
    if (len > 0 && len < (int)sizeof(buf))
    {
      (void)GNSS_VCP_PostText(buf);
    }

    if (status == HAL_OK)
    {
      if (who_am_i == MPU_WHO_AM_I_9250)
      {
        hmpu->model = MPU_MODEL_9250;
        hmpu->who_am_i = who_am_i;
        (void)GNSS_VCP_PostText("MPU detected: MPU-9250 (WHO_AM_I=0x71)");
        return HAL_OK;
      }
      else if (who_am_i == MPU_WHO_AM_I_6500)
      {
        hmpu->model = MPU_MODEL_6500;
        hmpu->who_am_i = who_am_i;
        (void)GNSS_VCP_PostText("MPU detected: MPU-6500 (WHO_AM_I=0x70)");
        return HAL_OK;
      }
      else
      {
        /* Device responded but unknown WHO_AM_I */
        len = snprintf(buf, sizeof(buf),
                       "MPU 0x%02X: unknown WHO_AM_I=0x%02X (expected 0x70/0x71)",
                       hmpu->i2c_addr, who_am_i);
        if (len > 0 && len < (int)sizeof(buf))
        {
          (void)GNSS_VCP_PostText(buf);
        }
        /* Continue to next address */
      }
    }
  }

  /* No supported device found at either address */
  hmpu->model = MPU_MODEL_UNKNOWN;
  hmpu->i2c_addr = 0;
  hmpu->who_am_i = 0;
  (void)GNSS_VCP_PostText("MPU fail: no supported device at 0x68 or 0x69");
  return HAL_ERROR;
}

/**
  * @brief  Initialize the MPU accelerometer and gyroscope.
  *         Performs reset, clock selection, range configuration, DLPF, sample rate.
  *         Reads back key configuration registers to verify.
  * @param  hmpu: Pointer to MPU_Handle_t with model and i2c_addr already set by MPU_Probe.
  * @retval HAL_OK on success, HAL_ERROR on I2C failure or readback mismatch,
  *         HAL_TIMEOUT on bus timeout.
  */
HAL_StatusTypeDef MPU_Init(MPU_Handle_t *hmpu)
{
  HAL_StatusTypeDef status;
  uint8_t reg_val;

  if (hmpu == NULL || hmpu->model == MPU_MODEL_UNKNOWN)
  {
    return HAL_ERROR;
  }

  /* 1. Reset the MPU */
  status = MPU_WriteReg(hmpu, MPU_REG_PWR_MGMT_1, MPU_PWR_MGMT_1_RESET);
  if (status != HAL_OK)
  {
    MPU_LogI2CError("reset write", status);
    return status;
  }
  HAL_Delay(MPU_DELAY_RESET_MS);

  /* 2. Wake device and select PLL with X gyro reference */
  status = MPU_WriteReg(hmpu, MPU_REG_PWR_MGMT_1, MPU_CLKSEL_PLL_X_GYRO);
  if (status != HAL_OK)
  {
    MPU_LogI2CError("CLKSEL write", status);
    return status;
  }
  HAL_Delay(MPU_DELAY_MODE_SWITCH_MS);

  /* 3. Disable I2C master (we use bypass mode for AK8963) */
  status = MPU_WriteReg(hmpu, MPU_REG_USER_CTRL, 0x00);
  if (status != HAL_OK)
  {
    MPU_LogI2CError("USER_CTRL write", status);
    return status;
  }

  /* 4. Configure sample rate divider for 100 Hz (1 kHz / (1 + 9) = 100 Hz) */
  status = MPU_WriteReg(hmpu, MPU_REG_SMPLRT_DIV, 9);
  if (status != HAL_OK)
  {
    MPU_LogI2CError("SMPLRT_DIV write", status);
    return status;
  }

  /* 5. Configure DLPF: gyro 41 Hz, accel 41 Hz (DLPF_CFG=3) */
  status = MPU_WriteReg(hmpu, MPU_REG_CONFIG, MPU_DLPF_CFG_41HZ);
  if (status != HAL_OK)
  {
    MPU_LogI2CError("CONFIG write", status);
    return status;
  }

  /* 6. Configure gyroscope range ±250°/s */
  status = MPU_WriteReg(hmpu, MPU_REG_GYRO_CONFIG, MPU_GYRO_FS_SEL_250);
  if (status != HAL_OK)
  {
    MPU_LogI2CError("GYRO_CONFIG write", status);
    return status;
  }

  /* 7. Configure accelerometer range ±2g */
  status = MPU_WriteReg(hmpu, MPU_REG_ACCEL_CONFIG, MPU_ACCEL_FS_SEL_2G);
  if (status != HAL_OK)
  {
    MPU_LogI2CError("ACCEL_CONFIG write", status);
    return status;
  }

  /* 8. Configure accelerometer DLPF (ACCEL_CONFIG2) */
  /* accel_fchoice_b = 0 (enable DLPF), DLPF_CFG = 3 (41 Hz) */
  status = MPU_WriteReg(hmpu, MPU_REG_ACCEL_CONFIG2, 0x03);
  if (status != HAL_OK)
  {
    MPU_LogI2CError("ACCEL_CONFIG2 write", status);
    return status;
  }

  /* 9. Enable all axes (PWR_MGMT_2) */
  status = MPU_WriteReg(hmpu, MPU_REG_PWR_MGMT_2, MPU_PWR_MGMT_2_ALL_AXES_ON);
  if (status != HAL_OK)
  {
    MPU_LogI2CError("PWR_MGMT_2 write", status);
    return status;
  }

  /* 10. Enable I2C bypass for AK8963 access (INT_PIN_CFG) */
  status = MPU_WriteReg(hmpu, MPU_REG_INT_PIN_CFG, MPU_INT_PIN_CFG_BYPASS_EN);
  if (status != HAL_OK)
  {
    MPU_LogI2CError("INT_PIN_CFG write", status);
    return status;
  }

  /* 11. Read back key registers to verify */
  uint8_t verify_regs[] = {
    MPU_REG_PWR_MGMT_1,
    MPU_REG_SMPLRT_DIV,
    MPU_REG_CONFIG,
    MPU_REG_GYRO_CONFIG,
    MPU_REG_ACCEL_CONFIG,
    MPU_REG_ACCEL_CONFIG2,
    MPU_REG_PWR_MGMT_2,
    MPU_REG_INT_PIN_CFG
  };
  uint8_t expected[] = {
    MPU_CLKSEL_PLL_X_GYRO,
    9,
    MPU_DLPF_CFG_41HZ,
    MPU_GYRO_FS_SEL_250,
    MPU_ACCEL_FS_SEL_2G,
    0x03,
    MPU_PWR_MGMT_2_ALL_AXES_ON,
    MPU_INT_PIN_CFG_BYPASS_EN
  };

  for (int i = 0; i < 8; i++)
  {
    status = MPU_ReadReg(hmpu, verify_regs[i], &reg_val);
    if (status != HAL_OK)
    {
      MPU_LogI2CError("verify read", status);
      return status;
    }
    if (reg_val != expected[i])
    {
      char buf[128];
      int len = snprintf(buf, sizeof(buf),
                         "MPU verify fail reg 0x%02X: got 0x%02X exp 0x%02X",
                         verify_regs[i], reg_val, expected[i]);
      if (len > 0 && len < (int)sizeof(buf))
      {
        (void)GNSS_VCP_PostText(buf);
      }
      return HAL_ERROR;
    }
  }

  /* Initialization successful */
  hmpu->initialized = 1;
  hmpu->valid = 0;
  hmpu->last_update_tick = 0;
  hmpu->next_poll_tick = HAL_GetTick() + MPU_POLL_INTERVAL_MS;
  hmpu->last_poll_tick = HAL_GetTick();
  hmpu->dt_s = 0.01f; /* nominal 100 Hz */
  hmpu->consec_failures = 0;
  hmpu->last_retry_tick = 0;
  hmpu->last_error = HAL_OK;

  /* Reset calibration state */
  hmpu->gyro_bias.x_dps = 0;
  hmpu->gyro_bias.y_dps = 0;
  hmpu->gyro_bias.z_dps = 0;
  hmpu->gyro_bias.valid = false;
  hmpu->mag_calib.x_ut = 0;
  hmpu->mag_calib.y_ut = 0;
  hmpu->mag_calib.z_ut = 0;
  hmpu->mag_calib.valid = false;
  hmpu->mag_factory_sens.x = 1.0f;
  hmpu->mag_factory_sens.y = 1.0f;
  hmpu->mag_factory_sens.z = 1.0f;
  hmpu->mag_soft_iron.x = 1.0f;
  hmpu->mag_soft_iron.y = 1.0f;
  hmpu->mag_soft_iron.z = 1.0f;
  /* Soft-iron defaults to identity with valid=true: no correction assumed.
   * Physical soft-iron calibration can override this if available. */
  hmpu->mag_soft_iron.valid = true;

  /* Initialize alignment matrix based on MPU model.
   * For MPU-9250, the AK8963 chip-frame mapping to MPU accel/gyro frame is
   * documented in the MPU-9250 datasheet (AN-MPU-9250-01):
   *   AK8963 +X -> MPU -Y  (AK X axis along MPU -Y)
   *   AK8963 +Y -> MPU +X  (AK Y axis along MPU +X)
   *   AK8963 +Z -> MPU -Z  (AK Z axis along MPU -Z)
   * This gives the rotation matrix (magnetometer -> accel/gyro frame):
   *   [  0,  1,  0]   MPU_X = AK_Y
   *   [ -1,  0,  0]   MPU_Y = -AK_X
   *   [  0,  0, -1]   MPU_Z = -AK_Z
   * For MPU-6500 (no magnetometer), alignment remains unknown. */
  if (hmpu->model == MPU_MODEL_9250)
  {
    hmpu->mag_align.m[0][0] =  0.0f; hmpu->mag_align.m[0][1] =  1.0f; hmpu->mag_align.m[0][2] =  0.0f;
    hmpu->mag_align.m[1][0] = -1.0f; hmpu->mag_align.m[1][1] =  0.0f; hmpu->mag_align.m[1][2] =  0.0f;
    hmpu->mag_align.m[2][0] =  0.0f; hmpu->mag_align.m[2][1] =  0.0f; hmpu->mag_align.m[2][2] = -1.0f;
    hmpu->mag_align.known = true;
  }
  else
  {
    hmpu->mag_align.m[0][0] = 1.0f; hmpu->mag_align.m[0][1] = 0.0f; hmpu->mag_align.m[0][2] = 0.0f;
    hmpu->mag_align.m[1][0] = 0.0f; hmpu->mag_align.m[1][1] = 1.0f; hmpu->mag_align.m[1][2] = 0.0f;
    hmpu->mag_align.m[2][0] = 0.0f; hmpu->mag_align.m[2][1] = 0.0f; hmpu->mag_align.m[2][2] = 1.0f;
    hmpu->mag_align.known = false;
  }
  hmpu->mag_available = false;  /* Will be set true only after successful mag init */
  hmpu->calib_state = 1; /* start gyro calibration */
  hmpu->calib_samples = 0;

  /* Reset orientation */
  hmpu->orientation.roll_deg = 0;
  hmpu->orientation.pitch_deg = 0;
  hmpu->orientation.heading_deg = 0;
  hmpu->orientation.heading_valid = false;
  hmpu->orientation.timestamp_ms = 0;

  return HAL_OK;
}

/**
  * @brief  Initialize the AK8963 magnetometer (MPU-9250 only).
  *         Uses correct datasheet sequence: power-down -> Fuse ROM -> read ASA -> power-down -> 100Hz 16-bit.
  * @param  hmpu: Pointer to MPU_Handle_t (must be MPU_MODEL_9250).
  * @retval HAL_OK on success, HAL_ERROR on I2C failure or WIA mismatch,
  *         HAL_TIMEOUT on bus timeout.
  */
HAL_StatusTypeDef MPU_InitMagnetometer(MPU_Handle_t *hmpu)
{
  HAL_StatusTypeDef status;
  uint8_t reg_val;

  if (hmpu == NULL || hmpu->model != MPU_MODEL_9250)
  {
    hmpu->mag_available = false;
    return HAL_ERROR; /* Not a 9250, no magnetometer */
  }

  /* Default: mag not available until proven otherwise */
  hmpu->mag_available = false;

  /* 1. Ensure MPU I2C master is disabled */
  status = MPU_ReadReg(hmpu, MPU_REG_USER_CTRL, &reg_val);
  if (status != HAL_OK)
  {
    MPU_LogI2CError("USER_CTRL read", status);
    return status;
  }
  if (reg_val & MPU_USER_CTRL_I2C_MST_EN)
  {
    status = MPU_WriteReg(hmpu, MPU_REG_USER_CTRL, 0x00);
    if (status != HAL_OK)
    {
      MPU_LogI2CError("USER_CTRL disable master", status);
      return status;
    }
    HAL_Delay(MPU_DELAY_MODE_SWITCH_MS);
  }

  /* 2. I2C bypass already enabled in MPU_Init (INT_PIN_CFG BYPASS_EN) */

  /* 3. Read AK8963 WIA register */
  status = AK8963_ReadReg(AK8963_REG_WIA, &reg_val);
  if (status != HAL_OK)
  {
    MPU_LogI2CError("AK8963 WIA read", status);
    return status;
  }

  if (reg_val != AK8963_WIA_EXPECTED)
  {
    char buf[64];
    int len = snprintf(buf, sizeof(buf),
                       "AK8963 WIA mismatch: got 0x%02X exp 0x48", reg_val);
    if (len > 0 && len < (int)sizeof(buf))
    {
      (void)GNSS_VCP_PostText(buf);
    }
    return HAL_ERROR;
  }

  (void)GNSS_VCP_PostText("AK8963 detected: WIA=0x48");

  /* 4. Power-down mode first (datasheet sequence) */
  status = AK8963_WriteReg(AK8963_REG_CNTL1, AK8963_CNTL1_MODE_PDOWN);
  if (status != HAL_OK)
  {
    MPU_LogI2CError("AK8963 power-down write", status);
    return status;
  }
  HAL_Delay(MPU_DELAY_MODE_SWITCH_MS);

  /* 5. Enter Fuse ROM access mode to read ASA values */
  status = AK8963_WriteReg(AK8963_REG_CNTL1, AK8963_CNTL1_MODE_FUSE);
  if (status != HAL_OK)
  {
    MPU_LogI2CError("AK8963 FUSE mode write", status);
    return status;
  }
  HAL_Delay(MPU_DELAY_MODE_SWITCH_MS);

  /* 6. Read ASA values (ASAX, ASAY, ASAZ) */
  uint8_t asa[3];
  status = AK8963_ReadRegs(AK8963_REG_ASAX, asa, 3);
  if (status != HAL_OK)
  {
    MPU_LogI2CError("AK8963 ASA read", status);
    return status;
  }

  /* Store ASA values in IMMUTABLE factory sensitivity structure */
  /* Sensitivity multiplier: (ASA + 128) / 256 */
  hmpu->mag_factory_sens.x = ((float)asa[0] + 128.0f) / 256.0f;
  hmpu->mag_factory_sens.y = ((float)asa[1] + 128.0f) / 256.0f;
  hmpu->mag_factory_sens.z = ((float)asa[2] + 128.0f) / 256.0f;

  char buf[64];
  int len = snprintf(buf, sizeof(buf),
                     "AK8963 ASA: X=0x%02X Y=0x%02X Z=0x%02X (sens: %.3f/%.3f/%.3f)",
                     asa[0], asa[1], asa[2],
                     hmpu->mag_factory_sens.x, hmpu->mag_factory_sens.y, hmpu->mag_factory_sens.z);
  if (len > 0 && len < (int)sizeof(buf))
  {
    (void)GNSS_VCP_PostText(buf);
  }

  /* 7. Return to power-down mode before configuring continuous mode */
  status = AK8963_WriteReg(AK8963_REG_CNTL1, AK8963_CNTL1_MODE_PDOWN);
  if (status != HAL_OK)
  {
    MPU_LogI2CError("AK8963 power-down write (2)", status);
    return status;
  }
  HAL_Delay(MPU_DELAY_MODE_SWITCH_MS);

  /* 8. Configure 16-bit continuous measurement mode at 100 Hz (CNTL1 = 0x16 per task) */
  status = AK8963_WriteReg(AK8963_REG_CNTL1, AK8963_CNTL1_100HZ_16BIT);
  if (status != HAL_OK)
  {
    MPU_LogI2CError("AK8963 CNTL1 write", status);
    return status;
  }
  HAL_Delay(MPU_DELAY_MODE_SWITCH_MS);

  /* 9. Verify CNTL1 */
  status = AK8963_ReadReg(AK8963_REG_CNTL1, &reg_val);
  if (status != HAL_OK || reg_val != AK8963_CNTL1_100HZ_16BIT)
  {
    MPU_LogI2CError("AK8963 CNTL1 verify", status);
    return HAL_ERROR;
  }

  /* Success - magnetometer is available */
  hmpu->mag_available = true;
  (void)GNSS_VCP_PostText("AK8963 initialized: CNTL1=0x16 (100Hz 16-bit)");

  return HAL_OK;
}

/**
  * @brief  Non-blocking periodic poll. Call from main loop at ~100 Hz.
  *         Performs 14-byte burst read from 0x3B (accel, temp, gyro).
  *         On MPU-9250 with mag_available, also reads magnetometer when ST1 data-ready.
  *         Returns HAL_OK if accel/gyro succeeded (mag failure tracked separately).
  * @param  hmpu: Pointer to MPU_Handle_t state structure.
  * @retval HAL_OK if accel/gyro read succeeded,
  *         HAL_ERROR if accel/gyro read failed,
  *         HAL_BUSY if not yet time for next poll.
  */
HAL_StatusTypeDef MPU_Poll(MPU_Handle_t *hmpu)
{
  uint32_t now;
  HAL_StatusTypeDef status;
  uint8_t burst_buf[14];

  if (hmpu == NULL || !hmpu->initialized)
  {
    return HAL_ERROR;
  }

  now = HAL_GetTick();

  /* Check if it's time for the next poll (handles tick wrap) */
  if ((int32_t)(now - hmpu->next_poll_tick) < 0)
  {
    return HAL_BUSY;  /* Not yet time */
  }

  /* Calculate actual dt */
  hmpu->dt_s = (float)(now - hmpu->last_poll_tick) * 0.001f;
  if (hmpu->dt_s <= 0.0f) hmpu->dt_s = 0.01f; /* fallback */
  if (hmpu->dt_s > 0.1f) hmpu->dt_s = 0.1f;   /* clamp to 10 Hz min */

  /* Schedule next poll */
  hmpu->next_poll_tick = now + MPU_POLL_INTERVAL_MS;
  hmpu->last_poll_tick = now;

  /* 14-byte burst read: 0x3B-0x48 (accel X/Y/Z, temp, gyro X/Y/Z) */
  status = MPU_ReadRegs(hmpu, MPU_REG_ACCEL_XOUT_H, burst_buf, 14);

  if (status == HAL_OK)
  {
    /* Parse big-endian int16 values */
    hmpu->raw.accel_x = (int16_t)((uint16_t)burst_buf[0] << 8 | burst_buf[1]);
    hmpu->raw.accel_y = (int16_t)((uint16_t)burst_buf[2] << 8 | burst_buf[3]);
    hmpu->raw.accel_z = (int16_t)((uint16_t)burst_buf[4] << 8 | burst_buf[5]);
    hmpu->raw.temp    = (int16_t)((uint16_t)burst_buf[6] << 8 | burst_buf[7]);
    hmpu->raw.gyro_x  = (int16_t)((uint16_t)burst_buf[8] << 8 | burst_buf[9]);
    hmpu->raw.gyro_y  = (int16_t)((uint16_t)burst_buf[10] << 8 | burst_buf[11]);
    hmpu->raw.gyro_z  = (int16_t)((uint16_t)burst_buf[12] << 8 | burst_buf[13]);

    /* Scale to physical units */
    hmpu->scaled.accel_x_g = MPU_RawAccelToG(hmpu->raw.accel_x);
    hmpu->scaled.accel_y_g = MPU_RawAccelToG(hmpu->raw.accel_y);
    hmpu->scaled.accel_z_g = MPU_RawAccelToG(hmpu->raw.accel_z);
    hmpu->scaled.gyro_x_dps = MPU_RawGyroToDPS(hmpu->raw.gyro_x);
    hmpu->scaled.gyro_y_dps = MPU_RawGyroToDPS(hmpu->raw.gyro_y);
    hmpu->scaled.gyro_z_dps = MPU_RawGyroToDPS(hmpu->raw.gyro_z);
    hmpu->scaled.temp_c = MPU_RawTempToC(hmpu->raw.temp);

    /* Run calibration if needed */
    if (hmpu->calib_state == 1)
    {
      MPU_CalibrateGyro(hmpu);
    }
    else if (hmpu->calib_state == 2)
    {
      MPU_CalibrateMag(hmpu);
    }

    /* Update orientation (uses calibrated gyro data).
     * For MPU-9250, heading will be updated again after mag read if calibration complete. */
    if (hmpu->calib_state >= 1) /* after gyro calibration starts */
    {
      MPU_UpdateOrientation(hmpu);
    }

    /* Magnetometer read (MPU-9250 only, only if mag_available) */
    if (hmpu->model == MPU_MODEL_9250 && hmpu->mag_available)
    {
      /* Read ST1 first to check data ready */
      HAL_StatusTypeDef mag_status = AK8963_ReadReg(AK8963_REG_ST1, &hmpu->mag_raw.st1);
      if (mag_status == HAL_OK && (hmpu->mag_raw.st1 & AK8963_ST1_DRDY))
      {
        /* Read 7 bytes: HXL, HXH, HYL, HYH, HZL, HZH, ST2 (little-endian) */
        uint8_t mag_buf[7];
        mag_status = AK8963_ReadRegs(AK8963_REG_HXL, mag_buf, 7);
        if (mag_status == HAL_OK)
        {
          hmpu->mag_raw.x = (int16_t)((uint16_t)mag_buf[1] << 8 | mag_buf[0]);
          hmpu->mag_raw.y = (int16_t)((uint16_t)mag_buf[3] << 8 | mag_buf[2]);
          hmpu->mag_raw.z = (int16_t)((uint16_t)mag_buf[5] << 8 | mag_buf[4]);
          hmpu->mag_raw.st2 = mag_buf[6];

          /* Apply factory sensitivity and convert to µT */
          MPU_ApplyMagSensitivity(hmpu, &hmpu->mag_raw, &hmpu->mag_scaled);

          /* Check for magnetic sensor overflow (HOFL in ST2) */
          if (hmpu->mag_raw.st2 & AK8963_ST2_HOFL)
          {
            hmpu->mag_scaled.valid = false;
          }

          /* Run mag calibration if needed */
          if (hmpu->calib_state == 2)
          {
            MPU_CalibrateMag(hmpu);
          }

          /* Update orientation with new mag data */
          if (hmpu->calib_state >= 3 && hmpu->mag_scaled.valid && hmpu->mag_calib.valid)
          {
            MPU_UpdateOrientation(hmpu);
          }
          else
          {
            /* Calibration incomplete or mag data invalid: heading is not available.
             * Explicitly invalidate heading_valid here so it cannot remain true
             * from a previous successful MPU_UpdateOrientation call. */
            hmpu->orientation.heading_valid = false;
            hmpu->orientation.heading_deg = 0.0f;
          }
        }
        else
        {
          /* Mag register read failed: invalidate immediately */
          hmpu->mag_scaled.valid = false;
          hmpu->orientation.heading_valid = false;
          hmpu->orientation.heading_deg = 0.0f;
        }
      }
      else
      {
        /* ST1 not ready (no DRDY) or ST1 read failed: invalidate immediately.
         * This prevents heading_valid from remaining true from a prior poll.
         * Overflow is also checked in the ST2 path above. */
        hmpu->mag_scaled.valid = false;
        hmpu->orientation.heading_valid = false;
        hmpu->orientation.heading_deg = 0.0f;
      }
    }

    /* Accel/gyro read succeeded */
    hmpu->last_update_tick = now;
    hmpu->valid = 1;
    hmpu->consec_failures = 0;
    hmpu->last_error = HAL_OK;
  }
  else
  {
    /* I2C failure on accel/gyro: mark data invalid and track failures */
    hmpu->valid = 0;
    hmpu->last_error = status;
    if (hmpu->consec_failures < 255)
    {
      hmpu->consec_failures++;
    }
    MPU_LogI2CError("burst read", status);
  }

  return status;  /* Return accel/gyro status only */
}

/**
  * @brief  Check if the driver has a valid recent accel/gyro reading.
  * @param  hmpu: Pointer to MPU_Handle_t state structure.
  * @retval 1 if valid, 0 otherwise.
  */
uint8_t MPU_IsValid(const MPU_Handle_t *hmpu)
{
  return (hmpu != NULL && hmpu->initialized && hmpu->valid) ? 1U : 0U;
}

/**
  * @brief  Get the detected model as a string.
  * @param  model: MPU_Model_t value.
  * @return String: "MPU-9250", "MPU-6500", or "Unknown".
  */
const char* MPU_ModelToString(MPU_Model_t model)
{
  switch (model)
  {
    case MPU_MODEL_9250: return "MPU-9250";
    case MPU_MODEL_6500: return "MPU-6500";
    default: return "Unknown";
  }
}

/**
  * @brief  One bring-up attempt: probe, then init accel/gyro, then mag (if 9250).
  * @param  hmpu: Pointer to MPU_Handle_t state structure.
  * @retval HAL_OK if accel/gyro initialized (mag failure doesn't cause error),
  *         HAL_ERROR if accel/gyro init failed,
  *         HAL_TIMEOUT if bus timeout on accel/gyro.
  */
HAL_StatusTypeDef MPU_BringUpAttempt(MPU_Handle_t *hmpu)
{
  HAL_StatusTypeDef status;

  if (hmpu == NULL)
  {
    return HAL_ERROR;
  }

  /* Increment attempt counter */
  if (hmpu->bringup_attempts < 255)
  {
    hmpu->bringup_attempts++;
  }

  /* Step 1: Probe for device */
  status = MPU_Probe(hmpu);
  if (status != HAL_OK)
  {
    hmpu->initialized = 0;
    return status;
  }

  /* Step 2: Initialize accel/gyro */
  status = MPU_Init(hmpu);
  if (status != HAL_OK)
  {
    hmpu->initialized = 0;
    return status;
  }

  /* Step 3: Initialize magnetometer (MPU-9250 only) */
  if (hmpu->model == MPU_MODEL_9250)
  {
    status = MPU_InitMagnetometer(hmpu);
    if (status != HAL_OK)
    {
      /* Mag init failed - keep accel/gyro running, mag_available cleared */
      char buf[64];
      int len = snprintf(buf, sizeof(buf),
                         "MPU mag init failed: accel/gyro only, status=%d", (int)status);
      if (len > 0 && len < (int)sizeof(buf))
      {
        (void)GNSS_VCP_PostText(buf);
      }
      /* Don't return error - accel/gyro are working, mag_available=false */
    }
  }

  /* Success - accel/gyro initialized */
  return HAL_OK;
}

/**
  * @brief  Set custom magnetometer alignment matrix for non-standard board mounting.
  *         The default alignment assumes standard MPU-9250 chip-frame mapping
  *         (AK8963 +X -> MPU -Y, AK8963 +Y -> MPU +X, AK8963 +Z -> MPU -Z).
  *         Call this after MPU_Init if the board mounting differs from standard.
  * @param  hmpu: Pointer to MPU_Handle_t state structure.
  * @param  align: 3x3 row-major matrix (magnetometer -> accel/gyro frame).
  * @note   Sets mag_align.known = true. Only meaningful for MPU-9250.
  */
void MPU_SetMagAlignment(MPU_Handle_t *hmpu, const float align[3][3])
{
  if (hmpu == NULL || hmpu->model != MPU_MODEL_9250)
  {
    return;
  }
  for (int i = 0; i < 3; i++)
  {
    for (int j = 0; j < 3; j++)
    {
      hmpu->mag_align.m[i][j] = align[i][j];
    }
  }
  hmpu->mag_align.known = true;
}

/* USER CODE BEGIN 1 */

/* USER CODE END 1 */