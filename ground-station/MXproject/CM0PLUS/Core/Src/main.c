/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
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
#include "main.h"
#include "app_subghz_phy.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
/*
 * CPU2 (Cortex-M0+) role in this dual-core project
 * -----------------------------------------------
 * The SubGHz_Phy PingPong application from STM32CubeWL (upstream example
 * Projects/NUCLEO-WL55JC/Applications/SubGHz_Phy/SubGHz_Phy_PingPong) is a
 * single-core CPU1 project. Here it runs on CPU2, which owns:
 *   - the SUBGHZ radio (SUBGHZSPI + SUBGHZ_Radio_IRQn) and the RF switch GPIOs,
 *   - the RTC used as the UTIL_TIMER/tick time base (RTC_LSECSS_IRQn on CPU2),
 *   - USART2 on PA2/PA3 (ST-LINK VCP) for the application trace, with
 *     DMA1 channel 5 (DMA1_Channel4_5_6_7_IRQn on CPU2).
 *
 * CPU1 stays the boot master: it configures the system clocks and releases
 * CPU2 with HAL_PWREx_ReleaseCore(PWR_CORE_CPU2) at the end of its own
 * initialisation, so no SystemClock_Config() is executed here - reconfiguring
 * MSI/PLL/LSE from CPU2 would fight with CPU1. Consequently CPU2 inherits the
 * clock tree programmed by CPU1 (MSI, 4 MHz) and uses the RTC (LSE) - not
 * SysTick - as its HAL time base (see the HAL_InitTick/HAL_Delay overloads in
 * sys_app.c), so the ping-pong timings are independent of the core clock.
 *
 * Result: CPU2 must be flashed at 0x08020000 (CM0PLUS_*.elf) and CPU1 at
 * 0x08000000 (CM4 *.elf); both images are required for the link to run.
 */
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  /* USER CODE BEGIN 2 */

  /* USER CODE END 2 */

  /* Initialize the SubGHz_Phy application (radio, sequencer, RTC time base,
     trace) - equivalent to MX_SubGHz_Phy_Init() of the upstream example. */
  MX_SubGHz_Phy_Init();

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */
    MX_SubGHz_Phy_Process();

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
