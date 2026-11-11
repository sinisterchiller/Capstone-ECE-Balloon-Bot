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

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "usart.h"
#include "nmea_rx.h"
#include "gnss_vcp.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* Firmware role of this image. The build passes -DFW_ROLE=PAYLOAD or
   -DFW_ROLE=GROUND (CM4/CMakeLists.txt, FW_ROLE in CMakePresets.json) and the
   preprocessor below compares FW_ROLE with the two names defined here. They
   must therefore be distinct numbers: an undefined identifier evaluates to 0
   in #if, so without these definitions every `FW_ROLE == PAYLOAD` and
   `FW_ROLE == GROUND` test would be 0 == 0 and the GROUND image would run the
   payload behaviour. The same two values are defined for CPU2 in
   CM0PLUS/SubGHz_Phy/App/subghz_phy_app.h; the two cores are separate images,
   so each keeps its own definition. */
#define PAYLOAD 1
#define GROUND  0

/* A missing FW_ROLE, or a numeric value that is neither PAYLOAD nor GROUND,
   must fail the build instead of silently compiling one of the two behaviours.
   Note that an identifier that is neither of these names also evaluates to 0
   and is therefore indistinguishable from GROUND in #if; only the build
   (FW_ROLE in CMakePresets.json / CM4/CMakeLists.txt) can guarantee the
   token, which is why FW_ROLE is required to be defined at all here. */
#if !defined(FW_ROLE)
#error "FW_ROLE must be defined by the build (CM4/CMakeLists.txt): PAYLOAD or GROUND"
#elif (FW_ROLE == PAYLOAD)
#elif (FW_ROLE == GROUND)
#else
#error "FW_ROLE must be PAYLOAD or GROUND"
#endif
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

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

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  /* USER CODE BEGIN 2 */
#if (FW_ROLE == PAYLOAD)
  /* GNSS receiver: USART1 RX-only (PB7, 9600 8N1) + interrupt reception */
  MX_USART1_UART_Init();
  NMEA_RX_Start();

  /* Shared ring used to hand the received sentences to CPU2, which owns the
     ST-LINK VCP. Initialised before CPU2 is released below. */
  GNSS_VCP_Init();
#endif /* FW_ROLE == PAYLOAD */
  /* GROUND has no GNSS receiver attached: USART1 is left uninitialised (reset
     state, no RX interrupt enabled), so no sentence is received or parsed and
     CPU2 is released below without a GNSS mailbox to drain. */
  /* USER CODE END 2 */

  /* Initialize leds */
  BSP_LED_Init(LED_BLUE);
  BSP_LED_Init(LED_GREEN);
  BSP_LED_Init(LED_RED);

  /* Initialize USER push-button, will be used to trigger an interrupt each time it's pressed.*/
  BSP_PB_Init(BUTTON_SW1, BUTTON_MODE_EXTI);
  BSP_PB_Init(BUTTON_SW2, BUTTON_MODE_EXTI);
  BSP_PB_Init(BUTTON_SW3, BUTTON_MODE_EXTI);

  /* The ST-LINK VCP (LPUART1 on PA2/PA3) is not initialised here: PA2/PA3 are
     owned by CPU2, which reconfigures them as USART2 115200 8N1 for the
     SubGHz_Phy trace once CPU1 releases it below. Leaving the pins alone on
     CPU1 keeps them free for CPU2 (and avoids CPU1 driving them as LPUART1). */

  /* Boot CPU2 */
  HAL_PWREx_ReleaseCore(PWR_CORE_CPU2);

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
#if (FW_ROLE == PAYLOAD)
    /* Hand the latest complete sentence, if any, to CPU2 for printing. */
    GNSS_VCP_Process();

    /* Periodically expire stale GPS fixes (check every loop iteration,
       the function itself handles the age threshold). */
    GNSS_VCP_CheckFixExpiration();
#endif /* FW_ROLE == PAYLOAD */
    /* GROUND has no periodic work on CPU1: CPU2 runs the receiver on its own
       time base, and CPU1 started no GPS reception (see USER CODE 2). */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE2);

  /** Initializes the CPU, AHB and APB buses clocks
  */
  /* LSE is started here, together with MSI, because CPU2 owns the RTC: its
     UTIL_TIMER_Init() -> MX_RTC_Init() selects RCC_RTCCLKSOURCE_LSE
     (CM0PLUS/Core/Src/rtc.c) and CPU2's HAL_GetTick() returns 0 until that
     time base exists (CM0PLUS/Core/Src/sys_app.c). With RTCCLK missing,
     HAL_RTC_Init() -> RTC_EnterInitMode() waits on RTC_ICSR_INITF with a tick
     that never advances and stalls CPU2 before its trace UART is set up.
     HAL_RCC_OscConfig() starts LSE, waits for LSERDY for up to
     LSE_STARTUP_TIMEOUT and returns HAL_TIMEOUT if the crystal does not start,
     so the oscillator must be running before HAL_PWREx_ReleaseCore() in main(). */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_MSI|RCC_OSCILLATORTYPE_LSE;
  RCC_OscInitStruct.MSIState = RCC_MSI_ON;
  RCC_OscInitStruct.MSICalibrationValue = RCC_MSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.MSIClockRange = RCC_MSIRANGE_6;
  RCC_OscInitStruct.LSEState = RCC_LSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure the SYSCLKSource, HCLK, PCLK1 and PCLK2 clocks dividers
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK3|RCC_CLOCKTYPE_HCLK2
                              |RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_MSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.AHBCLK2Divider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.AHBCLK3Divider = RCC_SYSCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
  {
    Error_Handler();
  }
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
