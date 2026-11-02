/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    subghz_phy_app.h
  * @author  MCD Application Team
  * @brief   Header of application of the SubGHz_Phy Middleware
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2021 STMicroelectronics.
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
#ifndef __SUBGHZ_PHY_APP_H__
#define __SUBGHZ_PHY_APP_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* MODEM type: one shall be 1 the other shall be 0 */
#define USE_MODEM_LORA  1
#define USE_MODEM_FSK   0

/*
 * RF_FREQUENCY — operating frequency in Hz.
 *
 * NUCLEO-WL55JC2 hardware: 470–520 MHz band.
 * The JC2 variant uses a 470 MHz ceramic filter. Do NOT use 868 MHz (EU)
 * or 915 MHz (US) on JC2 hardware — the filter will reject those frequencies.
 *
 * A compile-time check below rejects obviously wrong frequencies.
 */
#define RF_FREQUENCY                                470000000U /* Hz — JC2 band */

/*
 * TX_OUTPUT_POWER — output power in dBm.
 * Nucleo-WL55JC2: -7 to +15 dBm (no external PA).
 */
#ifndef TX_OUTPUT_POWER
#define TX_OUTPUT_POWER                             14        /* dBm */
#endif /* TX_OUTPUT_POWER */

/*
 * LoRa modulation parameters (ST default configuration):
 *   BANDWIDTH:  0 = 125 kHz  (LORA_BANDWIDTH == 0 → (1<<0)*125 = 125 kHz)
 *   SPREADING:  7  (SF7)
 *   CODING:     1  (4/5)
 *   PREAMBLE:   8 symbols
 */
#if (( USE_MODEM_LORA == 1 ) && ( USE_MODEM_FSK == 0 ))
#define LORA_BANDWIDTH                              0         /* [0: 125 kHz, 1: 250 kHz, 2: 500 kHz, 3: Reserved] */
#define LORA_SPREADING_FACTOR                       7         /* [SF7..SF12] */
#define LORA_CODINGRATE                             1         /* [1: 4/5, 2: 4/6, 3: 4/7, 4: 4/8] */
#define LORA_PREAMBLE_LENGTH                        8         /* Same for Tx and Rx */
#define LORA_SYMBOL_TIMEOUT                         5         /* Symbols */
#define LORA_FIX_LENGTH_PAYLOAD_ON                  false
#define LORA_IQ_INVERSION_ON                        false

#elif (( USE_MODEM_FSK == 1 ) && ( USE_MODEM_LORA == 0 ))

#define FSK_FDEV                                    25000     /* Hz */
#define FSK_DATARATE                                50000     /* bps */
#define FSK_BANDWIDTH                               50000     /* Hz */
#define FSK_PREAMBLE_LENGTH                         5         /* Same for Tx and Rx */
#define FSK_FIX_LENGTH_PAYLOAD_ON                   false

#else
#error "Please define a modem in the compiler subghz_phy_app.h."
#endif /* USE_MODEM_LORA | USE_MODEM_FSK */

/* Maximum payload length for the application protocol (bytes).
 * The ST PingPong example uses 64; we keep 64 for compatibility.
 * Actual min payload is 1 byte (zero-length rejected by MsgDeserialize).
 */
#define PAYLOAD_LEN                                 64

/*
 * Compile-time validation: reject frequencies outside JC2 hardware band.
 * JC2 hardware band: 470–520 MHz.
 */
#if (( RF_FREQUENCY < 470000000U ) || ( RF_FREQUENCY > 520000000U ))
#error "RF_FREQUENCY out of range for NUCLEO-WL55JC2 (470-520 MHz). Check your hardware variant."
#endif

/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* External variables --------------------------------------------------------*/
/* USER CODE BEGIN EV */

/* USER CODE END EV */

/* Exported macros -----------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
/**
  * @brief  Init Subghz Application
  */
void SubghzApp_Init(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

#ifdef __cplusplus
}
#endif

#endif /*__SUBGHZ_PHY_APP_H__*/
