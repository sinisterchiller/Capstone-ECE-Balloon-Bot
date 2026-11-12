/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    mpu.h
  * @brief   MPU-9250 / MPU-6500 IMU driver for STM32 HAL I2C.
  *          Provides detection, initialization, 100 Hz raw sampling,
  *          AK8963 magnetometer support (MPU-9250 only), and orientation
  *          calculation (roll, pitch, magnetic heading).
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

#ifndef __MPU_H__
#define __MPU_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "i2c.h"
#include <stdbool.h>
#include <stdint.h>

/* USER CODE BEGIN Private defines */

/* MPU I2C addresses (7-bit) - distinct from i2c.h shifted HAL addresses */
#define MPU_I2C_ADDR_PRIMARY     0x68U
#define MPU_I2C_ADDR_SECONDARY   0x69U

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
#define MPU_CLKSEL_PLL_X_GYRO    0x01U  /* PLL with X axis gyroscope reference */

/* GYRO_CONFIG bits */
#define MPU_GYRO_FS_SEL_250      0x00U  /* ±250 °/s */
#define MPU_GYRO_FS_SEL_500      0x08U  /* ±500 °/s */
#define MPU_GYRO_FS_SEL_1000     0x10U  /* ±1000 °/s */
#define MPU_GYRO_FS_SEL_2000     0x18U  /* ±2000 °/s */
#define MPU_GYRO_DLPF_CFG        0x07U  /* DLPF_CFG bits [2:0] */

/* ACCEL_CONFIG bits */
#define MPU_ACCEL_FS_SEL_2G      0x00U  /* ±2 g */
#define MPU_ACCEL_FS_SEL_4G      0x08U  /* ±4 g */
#define MPU_ACCEL_FS_SEL_8G      0x10U  /* ±8 g */
#define MPU_ACCEL_FS_SEL_16G     0x18U  /* ±16 g */

/* ACCEL_CONFIG2 bits */
#define MPU_ACCEL_DLPF_CFG       0x0FU  /* accel_fchoice_b[3] | DLPF_CFG[2:0] */
#define MPU_ACCEL_FCHOICE_B      0x08U  /* Accel FCHOICE_B bit */

/* CONFIG register DLPF_CFG for gyro */
#define MPU_DLPF_CFG_184HZ       0x01U  /* 184 Hz bandwidth, 1 kHz sample rate */
#define MPU_DLPF_CFG_92HZ        0x02U  /* 92 Hz bandwidth, 1 kHz sample rate */
#define MPU_DLPF_CFG_41HZ        0x03U  /* 41 Hz bandwidth, 1 kHz sample rate */
#define MPU_DLPF_CFG_20HZ        0x04U  /* 20 Hz bandwidth, 1 kHz sample rate */
#define MPU_DLPF_CFG_10HZ        0x05U  /* 10 Hz bandwidth, 1 kHz sample rate */
#define MPU_DLPF_CFG_5HZ         0x06U  /* 5 Hz bandwidth, 1 kHz sample rate */

/* INT_PIN_CFG bits */
#define MPU_INT_PIN_CFG_BYPASS_EN 0x02U  /* I2C bypass enable for AK8963 */
#define MPU_INT_PIN_CFG_I2C_MST_EN 0x20U /* I2C master enable */

/* USER_CTRL bits */
#define MPU_USER_CTRL_I2C_MST_EN  0x20U  /* I2C master enable */
#define MPU_USER_CTRL_I2C_MST_RST 0x02U  /* I2C master reset */

/* PWR_MGMT_2 bits */
#define MPU_PWR_MGMT_2_ALL_AXES_ON 0x00U /* All axes enabled */

/* AK8963 registers (MPU-9250 magnetometer) */
#define AK8963_ADDR              0x0CU   /* 7-bit address */
#define AK8963_REG_WIA           0x00U   /* Device ID, expect 0x48 */
#define AK8963_REG_INFO          0x01U
#define AK8963_REG_ST1           0x02U   /* Status 1: data ready, overflow */
#define AK8963_REG_HXL           0x03U   /* X-axis LSB */
#define AK8963_REG_HXH           0x04U   /* X-axis MSB */
#define AK8963_REG_HYL           0x05U   /* Y-axis LSB */
#define AK8963_REG_HYH           0x06U   /* Y-axis MSB */
#define AK8963_REG_HZL           0x07U   /* Z-axis LSB */
#define AK8963_REG_HZH           0x08U   /* Z-axis MSB */
#define AK8963_REG_ST2           0x09U   /* Status 2: overflow, HOFL */
#define AK8963_REG_CNTL1         0x0AU   /* Control 1: mode, resolution */
#define AK8963_REG_CNTL2         0x0BU   /* Control 2: soft reset */
#define AK8963_REG_ASTC          0x0CU   /* Self-test control */
#define AK8963_REG_TS1           0x0DU
#define AK8963_REG_TS2           0x0EU
#define AK8963_REG_I2CDIS        0x0FU
#define AK8963_REG_ASAX          0x10U   /* X-axis sensitivity adjustment */
#define AK8963_REG_ASAY          0x11U   /* Y-axis sensitivity adjustment */
#define AK8963_REG_ASAZ          0x12U   /* Z-axis sensitivity adjustment */

/* AK8963 CNTL1 bits - task mandates 0x16 for 100Hz 16-bit continuous */
#define AK8963_CNTL1_MODE_PDOWN  0x00U  /* Power-down mode */
#define AK8963_CNTL1_MODE_SINGLE 0x01U  /* Single measurement mode */
#define AK8963_CNTL1_MODE_CONT1  0x02U  /* Continuous measurement mode 1 (10 Hz) */
#define AK8963_CNTL1_MODE_CONT2  0x06U  /* Continuous measurement mode 2 (20 Hz) */
#define AK8963_CNTL1_MODE_CONT3  0x08U  /* Continuous measurement mode 3 (50 Hz) */
#define AK8963_CNTL1_MODE_CONT4  0x0AU  /* Continuous measurement mode 4 (100 Hz) */
#define AK8963_CNTL1_MODE_FUSE   0x0FU  /* Fuse ROM access mode */
#define AK8963_CNTL1_BIT_16      0x10U  /* 16-bit output (0 = 14-bit) */
/* Task-mandated value: 0x16 = 100Hz 16-bit continuous (per task1.md) */
#define AK8963_CNTL1_100HZ_16BIT 0x16U

/* AK8963 ST1 bits */
#define AK8963_ST1_DRDY          0x01U  /* Data ready */

/* AK8963 ST2 bits */
#define AK8963_ST2_HOFL          0x08U  /* Magnetic sensor overflow */

/* WIA expected value */
#define AK8963_WIA_EXPECTED      0x48U

/* Delays (ms) */
#define MPU_DELAY_POR_MS         100U   /* Power-on to first comms */
#define MPU_DELAY_RESET_MS       100U   /* Reset completion */
#define MPU_DELAY_MODE_SWITCH_MS 10U    /* Mode switch */
#define MPU_I2C_TIMEOUT_MS       10U    /* I2C transaction timeout */
#define MPU_POLL_INTERVAL_MS     10U    /* 100 Hz polling interval */

/* Sensor scaling factors */
#define MPU_ACCEL_SCALE_2G       16384.0f  /* LSB/g at ±2g */
#define MPU_GYRO_SCALE_250DPS    131.0f    /* LSB/(°/s) at ±250°/s */

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

/* USER CODE END Private defines */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

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
  float accel_x_g;      /* g */
  float accel_y_g;      /* g */
  float accel_z_g;      /* g */
  float gyro_x_dps;     /* °/s */
  float gyro_y_dps;     /* °/s */
  float gyro_z_dps;     /* °/s */
  float temp_c;         /* °C */
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
  float x_ut;   /* µT */
  float y_ut;   /* µT */
  float z_ut;   /* µT */
  bool  valid;  /* false if overflow (ST2 HOFL) or ST1 not ready */
} MPU_MagScaled_t;

/** Orientation output (roll, pitch, heading). */
typedef struct
{
  float roll_deg;       /* degrees, -180..+180 */
  float pitch_deg;      /* degrees, -90..+90 */
  float heading_deg;    /* degrees, 0..360 (magnetic, MPU-9250 only) */
  bool  heading_valid;  /* true only for MPU-9250 with valid mag calibration */
  uint32_t timestamp_ms;/* HAL_GetTick() when computed */
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
  float x;  /* (ASA + 128) / 256 multiplier */
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

/** Magnetometer soft-iron scale correction (diagonal matrix). */
typedef struct
{
  float x;  /* scale factor for X axis (default 1.0) */
  float y;  /* scale factor for Y axis (default 1.0) */
  float z;  /* scale factor for Z axis (default 1.0) */
  bool  valid; /* true if calibrated */
} MPU_MagSoftIron_t;

/** Magnetometer axis alignment matrix (3x3 row-major).
 *  Transforms magnetometer axes to accelerometer/gyro frame.
 *  Identity if unknown/uncalibrated (heading gated). */
typedef struct
{
  float m[3][3];
  bool  known; /* true if alignment is physically calibrated */
} MPU_MagAlign_t;

/** MPU driver state and latest readings. */
typedef struct
{
  /* Device identification */
  MPU_Model_t model;           /* MPU_MODEL_9250 or MPU_MODEL_6500 */
  uint8_t     i2c_addr;        /* 7-bit address that responded (0x68 or 0x69) */
  uint8_t     who_am_i;        /* Value read from WHO_AM_I register */

  /* Initialization state */
  uint8_t     initialized;     /* 1 = init sequence completed successfully */

  /* Latest valid raw data */
  MPU_RawData_t  raw;
  MPU_ScaledData_t scaled;

  /* Magnetometer (MPU-9250 only) */
  MPU_MagRaw_t           mag_raw;
  MPU_MagScaled_t        mag_scaled;
  MPU_MagFactorySens_t   mag_factory_sens;  /* Immutable ASA multipliers */
  MPU_MagCalib_t         mag_calib;         /* Hard-iron offsets */
  MPU_MagSoftIron_t      mag_soft_iron;     /* Soft-iron scale correction */
  MPU_MagAlign_t         mag_align;         /* Axis alignment to accel/gyro frame */
  bool                   mag_available;     /* Persistent gate: AK8963 usable */

  /* Orientation */
  MPU_Orientation_t orientation;

  /* Calibration */
  MPU_GyroBias_t   gyro_bias;
  uint8_t          calib_state;    /* 0=idle, 1=gyro_calibrating, 2=mag_calibrating, 3=done */
  uint16_t         calib_samples;  /* samples collected so far */

  /* Scheduler state */
  uint32_t next_poll_tick;    /* Next scheduled poll time (HAL_GetTick()) */
  uint32_t last_poll_tick;    /* Previous poll time for dt calculation */
  uint32_t last_update_tick;  /* HAL_GetTick() when data was last updated */
  float    dt_s;              /* Actual time between polls in seconds */

  /* Latest valid reading flag (accel/gyro only) */
  uint8_t valid;              /* 1 = accel/gyro data contains fresh valid reading */

  /* Error tracking */
  uint8_t  consec_failures;   /* Consecutive I2C failures on accel/gyro */
  uint32_t last_retry_tick;   /* Last reconnect attempt time */
  HAL_StatusTypeDef last_error; /* Last HAL error code on accel/gyro */

  /* Bring-up bookkeeping */
  uint8_t  bringup_attempts;
  uint32_t next_bringup_tick;

} MPU_Handle_t;

/* USER CODE END ET */

/* Exported functions prototypes ---------------------------------------------*/
/* USER CODE BEGIN EFP */

/**
  * @brief  Probe for MPU at supported addresses (0x68, 0x69).
  *         Reads WHO_AM_I (0x75) to identify model.
  * @param  hmpu: Pointer to MPU_Handle_t state structure (zeroed by caller).
  * @retval HAL_OK on success (device found and identified),
  *         HAL_ERROR if no supported device at either address,
  *         HAL_TIMEOUT if bus communication failed.
  * @note   Logs HAL status, error code, ISR, and WHO_AM_I value for each address.
  *         Does not modify hi2c2 configuration.
  */
HAL_StatusTypeDef MPU_Probe(MPU_Handle_t *hmpu);

/**
  * @brief  Initialize the MPU accelerometer and gyroscope.
  *         Performs reset, clock selection, range configuration, DLPF, sample rate.
  *         Reads back key configuration registers to verify.
  * @param  hmpu: Pointer to MPU_Handle_t with model and i2c_addr already set by MPU_Probe.
  * @retval HAL_OK on success, HAL_ERROR on I2C failure or readback mismatch,
  *         HAL_TIMEOUT on bus timeout.
  * @note   Caller guarantees MPU_DELAY_POR_MS has elapsed since power-on.
  *         This function waits only for datasheet mode-switch delays.
  */
HAL_StatusTypeDef MPU_Init(MPU_Handle_t *hmpu);

/**
  * @brief  Initialize the AK8963 magnetometer (MPU-9250 only).
  *         Disables MPU I2C master, enables bypass, configures AK8963 for
  *         16-bit continuous 100 Hz mode (CNTL1=0x16), reads factory sensitivity.
  * @param  hmpu: Pointer to MPU_Handle_t (must be MPU_MODEL_9250).
  * @retval HAL_OK on success, HAL_ERROR on I2C failure or WIA mismatch,
  *         HAL_TIMEOUT on bus timeout.
  * @note   If initialization fails, mag_available is cleared; accel/gyro remain operational.
  *         Heading will be reported as unavailable.
  */
HAL_StatusTypeDef MPU_InitMagnetometer(MPU_Handle_t *hmpu);

/**
  * @brief  Non-blocking periodic poll. Call from main loop at ~100 Hz.
  *         Performs 14-byte burst read from 0x3B (accel, temp, gyro).
  *         On MPU-9250 with mag_available, also reads magnetometer when ST1 data-ready.
  *         Updates orientation (roll, pitch, heading) using complementary filter.
  * @param  hmpu: Pointer to MPU_Handle_t state structure.
  * @retval HAL_OK if accel/gyro read succeeded (mag failure tracked separately),
  *         HAL_ERROR if accel/gyro read failed,
  *         HAL_BUSY if not yet time for next poll.
  */
HAL_StatusTypeDef MPU_Poll(MPU_Handle_t *hmpu);

/**
  * @brief  Check if the driver has a valid recent accel/gyro reading.
  * @param  hmpu: Pointer to MPU_Handle_t state structure.
  * @retval 1 if valid, 0 otherwise.
  */
uint8_t MPU_IsValid(const MPU_Handle_t *hmpu);

/**
  * @brief  Get the detected model as a string.
  * @param  model: MPU_Model_t value.
  * @return String: "MPU-9250", "MPU-6500", or "Unknown".
  */
const char* MPU_ModelToString(MPU_Model_t model);

/**
  * @brief  Set custom magnetometer alignment matrix for non-standard board mounting.
  *         The default alignment assumes standard MPU-9250 chip-frame mapping
  *         (AK8963 +X -> MPU -Y, AK8963 +Y -> MPU +X, AK8963 +Z -> MPU -Z).
  *         Call this after MPU_Init if the board mounting differs from standard.
  * @param  hmpu: Pointer to MPU_Handle_t state structure.
  * @param  align: 3x3 row-major matrix (magnetometer -> accel/gyro frame).
  * @note   Sets mag_align.known = true. Only meaningful for MPU-9250.
  */
void MPU_SetMagAlignment(MPU_Handle_t *hmpu, const float align[3][3]);

/**
  * @brief  One bring-up attempt: probe, then init accel/gyro, then mag (if 9250).
  * @param  hmpu: Pointer to MPU_Handle_t state structure.
  * @retval HAL_OK if fully initialized (accel/gyro + mag if 9250),
  *         HAL_ERROR if accel/gyro init failed,
  *         HAL_TIMEOUT if bus timeout on accel/gyro.
  * @note   Non-blocking: each sub-step is a bounded I2C transaction plus
  *         datasheet delays. Called from main loop until success or budget spent.
  *         Magnetometer failure does not cause error return; mag_available is cleared.
  */
HAL_StatusTypeDef MPU_BringUpAttempt(MPU_Handle_t *hmpu);

/* USER CODE END EFP */

#ifdef __cplusplus
}
#endif

#endif /* __MPU_H__ */