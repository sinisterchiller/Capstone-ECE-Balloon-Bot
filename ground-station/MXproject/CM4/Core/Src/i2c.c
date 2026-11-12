/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    i2c.c
  * @brief   This file provides code for the configuration
  *          of the I2C instances.
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
/* Includes ------------------------------------------------------------------*/
#include "i2c.h"
#include "gnss_vcp.h"
#include <stdio.h>

/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

I2C_HandleTypeDef hi2c2;

/* I2C2 init function */
void MX_I2C2_Init(void)
{
  /* USER CODE BEGIN I2C2_Init 0 */

  /* USER CODE END I2C2_Init 0 */

  /* USER CODE BEGIN I2C2_Init 1 */

  /* USER CODE END I2C2_Init 1 */
  hi2c2.Instance = I2C2;
  hi2c2.Init.Timing = I2C2_TIMING_100KHZ;
  hi2c2.Init.OwnAddress1 = 0;
  hi2c2.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c2.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c2.Init.OwnAddress2 = 0;
  hi2c2.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
  hi2c2.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c2.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c2) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Analogue filter
  */
  if (HAL_I2CEx_ConfigAnalogFilter(&hi2c2, I2C_ANALOGFILTER_ENABLE) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Digital filter
  */
  if (HAL_I2CEx_ConfigDigitalFilter(&hi2c2, 0) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C2_Init 2 */

  /* USER CODE END I2C2_Init 2 */
}

/* USER CODE BEGIN 1 */

/**
 * @brief  Diagnostic probe at a specific I2C address.
 *         Performs a single IsDeviceReady call, captures HAL status,
 *         HAL error code, and I2C2->ISR atomically before subsequent
 *         operations can overwrite them.
 * @param  addr_shifted: 7-bit address left-shifted by 1 (HAL convention).
 * @retval HAL_StatusTypeDef from IsDeviceReady.
 */
HAL_StatusTypeDef I2C2_ProbeAddrDiagnostic(uint16_t addr_shifted)
{
  HAL_StatusTypeDef status;

  /* Single bounded transaction: 3 trials, 10 ms timeout each. */
  status = HAL_I2C_IsDeviceReady(&hi2c2, addr_shifted, 3, I2C2_PROBE_TIMEOUT_MS);

  /* Capture diagnostics immediately before any other I2C operation. */
  uint32_t hal_error = HAL_I2C_GetError(&hi2c2);
  uint32_t isr = hi2c2.Instance->ISR;

  /* Log one bounded diagnostic line: status, error, ISR, address. */
  char buf[104];
  int len = snprintf(buf, sizeof(buf),
                     "MPU probe 0x%02X: status=%d err=0x%08lX ISR=0x%08lX",
                     addr_shifted >> 1, (int)status,
                     (unsigned long)hal_error, (unsigned long)isr);
  if (len > 0 && len < (int)sizeof(buf))
  {
    (void)GNSS_VCP_PostText(buf);
  }

  return status;
}

/**
 * @brief  Reads WHO_AM_I register (0x75) at a specific I2C address.
 * @param  addr_shifted: 7-bit address left-shifted by 1 (HAL convention).
 * @param  who_am_i: Output pointer for WHO_AM_I value.
 * @retval HAL_StatusTypeDef from Mem_Read.
 */
HAL_StatusTypeDef I2C2_ReadChipIdAtAddr(uint16_t addr_shifted, uint8_t *who_am_i)
{
  if (who_am_i == NULL)
  {
    return HAL_ERROR;
  }

  /* Single register read with standard timeout. */
  HAL_StatusTypeDef status = HAL_I2C_Mem_Read(&hi2c2, addr_shifted,
                                                MPU_REG_WHO_AM_I,
                                                I2C_MEMADD_SIZE_8BIT,
                                                who_am_i, 1,
                                                I2C2_READ_TIMEOUT_MS);

  if (status == HAL_OK)
  {
    char buf[104];
    int len = snprintf(buf, sizeof(buf),
                       "MPU WHO_AM_I 0x%02X: 0x%02X",
                       addr_shifted >> 1, *who_am_i);
    if (len > 0 && len < (int)sizeof(buf))
    {
      (void)GNSS_VCP_PostText(buf);
    }
  }
  else
  {
    /* Capture error diagnostics for the WHO_AM_I read failure. */
    uint32_t hal_error = HAL_I2C_GetError(&hi2c2);
    uint32_t isr = hi2c2.Instance->ISR;
    char buf[104];
    int len = snprintf(buf, sizeof(buf),
                       "MPU WHO_AM_I 0x%02X read fail: status=%d err=0x%08lX ISR=0x%08lX",
                       addr_shifted >> 1, (int)status,
                       (unsigned long)hal_error, (unsigned long)isr);
    if (len > 0 && len < (int)sizeof(buf))
    {
      (void)GNSS_VCP_PostText(buf);
    }
  }

  return status;
}

/**
 * @brief  Direct diagnostic WHO_AM_I read at given address with 100 ms timeout.
 *         Captures HAL status, error code, and ISR before any other HAL call.
 *         Logs: "MPU direct 0xXX: status=... err=... ISR=... id=..." (id only on success).
 * @param  addr_shifted: 7-bit address left-shifted by 1 (HAL convention).
 * @param  who_am_i: Output pointer for WHO_AM_I value (only written on HAL_OK).
 * @retval HAL_StatusTypeDef from Mem_Read.
 */
HAL_StatusTypeDef I2C2_ReadChipIdDirectDiagnostic(uint16_t addr_shifted, uint8_t *who_am_i)
{
  if (who_am_i == NULL)
  {
    return HAL_ERROR;
  }

  /* Single bounded WHO_AM_I read with 100 ms timeout for diagnostic. */
  HAL_StatusTypeDef status = HAL_I2C_Mem_Read(&hi2c2, addr_shifted,
                                                MPU_REG_WHO_AM_I,
                                                I2C_MEMADD_SIZE_8BIT,
                                                who_am_i, 1,
                                                100U);

  /* Capture diagnostics immediately before any other I2C operation. */
  uint32_t hal_error = HAL_I2C_GetError(&hi2c2);
  uint32_t isr = hi2c2.Instance->ISR;

  /* Log one bounded diagnostic line: id only on success. */
  char buf[128];
  int len;
  if (status == HAL_OK)
  {
    len = snprintf(buf, sizeof(buf),
                   "MPU direct 0x%02X: status=%d err=0x%08lX ISR=0x%08lX id=0x%02X",
                   addr_shifted >> 1, (int)status,
                   (unsigned long)hal_error, (unsigned long)isr,
                   *who_am_i);
  }
  else
  {
    len = snprintf(buf, sizeof(buf),
                   "MPU direct 0x%02X: status=%d err=0x%08lX ISR=0x%08lX",
                   addr_shifted >> 1, (int)status,
                   (unsigned long)hal_error, (unsigned long)isr);
  }
  if (len > 0 && len < (int)sizeof(buf))
  {
    (void)GNSS_VCP_PostText(buf);
  }

  return status;
}

/* USER CODE END 1 */