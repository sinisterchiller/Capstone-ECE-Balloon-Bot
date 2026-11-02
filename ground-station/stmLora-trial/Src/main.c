/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body — LoRa Ping-Pong with UART diagnostics.
  *
  *                    Based on ST's SubGHz_Phy_PingPong example for NUCLEO-WL55JC.
  *                    Main.c handles full init sequence and the main loop.
  *                    Application messaging (PING/PONG protocol) lives in
  *                    SubGHz_Phy/App/subghz_phy_app.c inside PingPong_Process().
  *
  *                    Full init sequence:
  *                      1. HAL_Init()
  *                      2. SystemClock_Config()
  *                      3. MX_GPIO_Init()
  *                      4. MX_USART2_UART_Init()
  *                      5. vcom_Init()
  *                      6. MX_SubGHz_Phy_Init()
  *                      7. Main loop: MX_SubGHz_Phy_Process()
  *
  *                    Role and TX selection (compile-time):
  *                      Debug preset: edit Inc/app_build_config.h, then
  *                        configure → build → flash (APP_CONFIG_FROM_SOURCE=ON).
  *                      Other presets: use -DAPP_ROLE_INITIATOR=0|1
  *                        -DAPP_TX_ENABLED=0|1 in cmake command (OFF).
  *
  *                    UART diagnostics (115200 8N1, USART2):
  *                      Boot message with role, frequency, TX state
  *                      Per-packet TX/RX events with RSSI/SNR
  *                      Radio diagnostics every ~3 s (chip mode, RSSI stats)
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2021 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "subghz_phy_app.h"    /* for RF_FREQUENCY, TX_OUTPUT_POWER */
#include "app_subghz_phy.h"    /* MX_SubGHz_Phy_Init, MX_SubGHz_Phy_Process */
#include "app_build_config.h"  /* APP_ROLE_INITIATOR, APP_TX_ENABLED — single-source config */
#include "radio.h"             /* for Radio.GetStatus, RadioState_t */
#include "radio_driver.h"      /* for SUBGRF_GetStatus, RadioPhyStatus_t */
#include "gpio.h"
#include "usart_if.h"
#include "stm32wlxx_nucleo.h"  /* for BSP_LED_On, BSP_LED_Toggle, BOARD_LED_GREEN/RED */
#include <string.h>

/* ========================================================================== */
/*  Forward declarations                                                      */
/* ========================================================================== */

void SystemClock_Config(void);

/* ========================================================================== */
/*  UART diagnostics helpers                                                  */
/* ========================================================================== */

static void UartPrint(const char *str)
{
  if (str == NULL) return;
  vcom_Trace((uint8_t *)str, (uint16_t)strlen(str));
}

static void UartNewline(void)
{
  UartPrint("\r\n");
}

static void UartSeparator(void)
{
  UartPrint("  ----------------------------------------\r\n");
}

/**
  * @brief  Minimal integer-to-decimal printer (no printf dependency).
  */
static void UartPrintInt32(int32_t value)
{
  char str[16];
  uint8_t idx = 0;

  if (value < 0)
  {
    UartPrint("-");
    value = -value;
  }

  if (value == 0)
  {
    UartPrint("0");
  }
  else
  {
    while (value > 0 && idx < 15)
    {
      str[idx++] = (char)('0' + (value % 10));
      value /= 10;
    }
    for (uint8_t i = idx; i > 0; i--)
    {
      char c = str[i - 1];
      vcom_Trace((uint8_t *)&c, 1);
    }
  }
}

/**
  * @brief  Minimal radio state string (inline, no ring buffer dependency).
  */
static const char *RadioStateStr(RadioState_t state)
{
  switch (state)
  {
    case 0: return "IDLE";
    case 1: return "RX";
    case 2: return "TX";
    case 3: return "CAD";
    default: return "?";
  }
}

/* ========================================================================== */
/*  Radio diagnostics                                                         */
/* ========================================================================== */

/**
  * @brief  ChipMode string from SUBGRF_GetStatus().Fields.ChipMode.
  *
  *         Per ST RM0453 Get_Status command (3-bit ChipMode field, bits [6:4]):
  *           0x2 = STBY_RC,  0x3 = STBY_XOSC,  0x4 = FS,
  *           0x5 = RX,       0x6 = TX.
  *         Codes 0, 1, 7 are reserved.
  */
static const char *ChipModeStr(uint8_t chip_mode)
{
  switch (chip_mode)
  {
    case 0x02: return "STBY_RC";
    case 0x03: return "STBY_XOSC";
    case 0x04: return "FS";
    case 0x05: return "RX";
    case 0x06: return "TX";
    default:   return "???";
  }
}

/* ========================================================================== */
/*  Main entry point                                                          */
/* ========================================================================== */

/**
  * @brief  Main application entry point.
  *
  *         Full ST init sequence:
  *           1. HAL_Init() — reset peripherals, configure SysTick
  *           2. SystemClock_Config() — configure MSI/PLL
  *           3. MX_GPIO_Init() — LED and other GPIOs
  *           4. MX_USART2_UART_Init() — UART for diagnostics
  *           5. vcom_Init() — advanced trace layer
  *           6. MX_SubGHz_Phy_Init() — SubGHz_Phy middleware init
  *           7. Main loop: MX_SubGHz_Phy_Process()
  */
int main(void)
{
  /* 1. HAL initialization — clock setup, SysTick, etc. */
  HAL_Init();

  /* 2. Configure system clock (MSI @ 4 MHz, no PLL) */
  SystemClock_Config();

  /* 3. Initialize GPIO (LEDs, buttons, probes) */
  MX_GPIO_Init();

  /* 4. Initialize USART2 for diagnostics (115200 8N1) */
  MX_USART2_UART_Init();

  /* 5. Initialize advanced trace (vcom_Trace) */
  vcom_Init(NULL);
  vcom_ReceiveInit(NULL);

  UartSeparator();
  UartNewline();
  UartPrint("  STM32WL55 LoRa Ping-Pong\r\n");
  UartPrint("  Board: NUCLEO-WL55JC2\r\n");
  UartPrint("  Role:  ");
  UartPrint((APP_ROLE_INITIATOR != 0U) ? "INITIATOR" : "RESPONDER");
  UartNewline();
  UartPrint("  Freq:  ");
  UartPrintInt32((int32_t)(RF_FREQUENCY / 1000000L));
  UartPrint(" MHz\r\n");
  UartPrint("  TX:    ");
  UartPrint((APP_TX_ENABLED != 0U) ? "ENABLED" : "DISABLED");
  UartNewline();
  UartPrint("  BW:    125kHz SF7 CR4/5\r\n");
  UartPrint("  Preamble: 8 symbols\r\n");
  UartSeparator();

  /* 6. Initialize SubGHz_Phy middleware (radio init, callbacks, RX) */
  UartPrint("  Initializing SubGHz_Phy middleware...\r\n");
  MX_SubGHz_Phy_Init();
  UartPrint("  SubGHz_Phy initialized OK\r\n");

  /* Post-init radio state diagnostic */
  UartPrint("  Post-init radio state: ");
  UartPrint(RadioStateStr(Radio.GetStatus()));
  UartNewline();

  UartSeparator();
  UartPrint("  Starting ");
  UartPrint((APP_ROLE_INITIATOR != 0U) ? "INITIATOR" : "RESPONDER");
  UartPrint(" task...\r\n");
  UartPrint("  TX is ");
  UartPrint((APP_TX_ENABLED != 0U) ? "ENABLED" : "DISABLED");
  UartPrint(". Waiting for radio events.\r\n");
  UartSeparator();

  /* Turn on green LED to indicate running */
  BSP_LED_On(LED_GREEN);

  /* 7. Main loop — process radio events via ST sequencer */
  uint32_t rssi_sample_tick = 0;        /* ~50 ms RSSI sample timer   */
  uint32_t diag_tick        = 0;        /* ~3 s diagnostic report     */
  int16_t  rssi_min         = 0x7FFF;   /* start at max (no signal)   */
  int16_t  rssi_max         = -0x8000;  /* start at min               */
  int16_t  rssi_last        = 0;
  uint32_t rssi_count       = 0;

  while (1)
  {
    MX_SubGHz_Phy_Process();
    HAL_Delay(10);
    uint32_t tick = HAL_GetTick();

    /* RSSI sample (~50 ms cadence, only when RX_RUNNING).
     * Radio.Rssi(MODEM_LORA) reads the instantaneous RSSI register —
     * it does NOT change radio state.
     */
    if ((uint32_t)(tick - rssi_sample_tick) >= 50)
    {
      rssi_sample_tick = tick;
      if (Radio.GetStatus() == RF_RX_RUNNING)
      {
        int16_t rssi = Radio.Rssi(MODEM_LORA);
        rssi_last = rssi;
        rssi_count++;
        if (rssi < rssi_min) rssi_min = rssi;
        if (rssi > rssi_max) rssi_max = rssi;
      }
    }

    /* Periodic radio diagnostics every ~3 s: chip mode, RSSI stats.
     * Uses unsigned HAL_GetTick arithmetic for wrap-safe timing.
     * Single SUBGRF_GetStatus() call; prints raw ChipMode/CmdStatus
     * so unexpected mode or command errors are visible.
     */
    if ((uint32_t)(tick - diag_tick) >= 3000)
    {
      diag_tick = tick;

      RadioPhyStatus_t phy = SUBGRF_GetStatus();
      uint8_t chip_mode = phy.Fields.ChipMode;

      UartSeparator();
      UartPrint("  [DIAG] chip_mode=");
      UartPrint(ChipModeStr(chip_mode));
      UartPrintInt32((int32_t)chip_mode);
      UartPrint(" cmd_status=");
      UartPrintInt32((int32_t)phy.Fields.CmdStatus);
      UartPrint(" driver_state=");
      UartPrint(RadioStateStr(Radio.GetStatus()));
      UartPrint(" rssi_min=");
      UartPrintInt32((int32_t)rssi_min);
      UartPrint(" max=");
      UartPrintInt32((int32_t)rssi_max);
      UartPrint(" last=");
      UartPrintInt32((int32_t)rssi_last);
      UartPrint(" count=");
      UartPrintInt32((int32_t)rssi_count);
      UartNewline();

      /* Reset RSSI accumulators for next interval */
      rssi_min   = 0x7FFF;
      rssi_max   = -0x8000;
      rssi_last  = 0;
      rssi_count = 0;
    }
  }
}

/* ========================================================================== */
/*  System clock configuration                                                */
/* ========================================================================== */

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure LSE Drive Capability
  */
  HAL_PWR_EnableBkUpAccess();
  __HAL_RCC_LSEDRIVE_CONFIG(RCC_LSEDRIVE_LOW);

  /** Configure the main internal regulator output voltage
  */
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_LSE|RCC_OSCILLATORTYPE_MSI;
  RCC_OscInitStruct.LSEState = RCC_LSE_ON;
  RCC_OscInitStruct.MSIState = RCC_MSI_ON;
  RCC_OscInitStruct.MSICalibrationValue = RCC_MSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.MSIClockRange = RCC_MSIRANGE_11;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Enable MSI Auto calibration
  */
  HAL_RCCEx_EnableMSIPLLMode();

  /** Configure the SYSCLKSource, HCLK, PCLK1 and PCLK2 clocks dividers
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK3|RCC_CLOCKTYPE_HCLK
                              |RCC_CLOCKTYPE_SYSCLK|RCC_CLOCKTYPE_PCLK1
                              |RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_MSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.AHBCLK3Divider = RCC_SYSCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/* ========================================================================== */
/*  OS / HAL hooks (only those not already provided by ST HAL)                */
/* ========================================================================== */

/**
  * @brief  Error handler — called on HAL errors.
  *
  *         Blinks the red LED (LED3) to indicate a fault.
  *         If UART diagnostics are available, prints an error marker first.
  */
void Error_Handler(void)
{
  UartPrint("  [FATAL ERROR] halted\r\n");
  __disable_irq();
  while (1)
  {
    BSP_LED_Toggle(LED_RED);
    HAL_Delay(200);
  }
}

/**
  * @brief  Error callback — called on HAL errors.
  */
__WEAK void HAL_Error_Callback(void)
{
  UartPrint("  [HAL ERROR] system error\r\n");
  while (1)
  {
    BSP_LED_Toggle(LED_RED);
    HAL_Delay(100);
  }
}

