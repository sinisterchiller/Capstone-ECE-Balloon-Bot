/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    i2c.h
  * @brief   This file contains all the function prototypes for
  *          the i2c.c file
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __I2C_H__
#define __I2C_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

extern I2C_HandleTypeDef hi2c2;

/* USER CODE BEGIN Private defines */

/* MPU 7-bit addresses: 0x68 (AD0 low) and 0x69 (AD0 high). HAL expects shifted address. */
#define MPU_ADDR_PRIMARY       (0x68U << 1)
#define MPU_ADDR_SECONDARY     (0x69U << 1)

/* MPU WHO_AM_I register address. */
#define MPU_REG_WHO_AM_I       0x75U

/* I2C2 timing register value (I2C_TIMINGR) for standard mode at 4 MHz PCLK.
 * The value 0x00201313 decodes as:
 *   PRESC  = (value >> 28) & 0xF  = 0
 *   SCLDEL = (value >> 20) & 0xF  = 2
 *   SDADEL = (value >> 16) & 0xF  = 0
 *   SCLH   = (value >>  8) & 0xFF = 0x13 (19)
 *   SCLL   =  value        & 0xFF = 0x13 (19)
 * 100 kHz is the nominal target, but the actual SCL frequency also depends on
 * the bus rise/fall times, which are not known here and have not been verified
 * with a timing calculator, so no measured frequency is claimed. The value is
 * left unchanged until that verification is done.
 */
#define I2C2_TIMING_100KHZ     0x00201313U

/* Probe timeout in ms */
#define I2C2_PROBE_TIMEOUT_MS  10U

/* Single-register read timeout in ms. */
#define I2C2_READ_TIMEOUT_MS   10U

/* USER CODE END Private defines */

void MX_I2C2_Init(void);

/* Diagnostic probe: performs IsDeviceReady at given 7-bit address (shifted),
 * captures HAL status, HAL error code, and I2C2->ISR in one bounded transaction.
 * Does not modify configured sensor address. Returns HAL status of the probe.
 */
HAL_StatusTypeDef I2C2_ProbeAddrDiagnostic(uint16_t addr_shifted);

/* Reads CHIP_ID (reg 0x00) at given 7-bit address (shifted).
 * Returns HAL_OK and sets *chip_id on success; otherwise returns error.
 */
HAL_StatusTypeDef I2C2_ReadChipIdAtAddr(uint16_t addr_shifted, uint8_t *chip_id);

/**
 * @brief  Direct diagnostic CHIP_ID read at given address with 100 ms timeout.
 *         Captures HAL status, error code, and ISR before any other HAL call.
 *         Logs: "BNO direct 0xXX: status=... err=... ISR=... id=..." (id only on success).
 * @param  addr_shifted: 7-bit address left-shifted by 1 (HAL convention).
 * @param  chip_id: Output pointer for CHIP_ID value (only written on HAL_OK).
 * @retval HAL_StatusTypeDef from Mem_Read.
 */
HAL_StatusTypeDef I2C2_ReadChipIdDirectDiagnostic(uint16_t addr_shifted, uint8_t *chip_id);

/* USER CODE BEGIN Prototypes */

/* USER CODE END Prototypes */

#ifdef __cplusplus
}
#endif

#endif /* __I2C_H__ */