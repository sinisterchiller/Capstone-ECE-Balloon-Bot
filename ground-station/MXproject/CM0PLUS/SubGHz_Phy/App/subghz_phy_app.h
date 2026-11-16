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
/* Firmware role of this image. The build passes -DFW_ROLE=PAYLOAD or
   -DFW_ROLE=GROUND (CM0PLUS/CMakeLists.txt, preset FW_ROLE) and the
   preprocessor compares the resulting identifier with the two names below.
   They must therefore be defined as distinct numbers: an undefined identifier
   evaluates to 0 in #if, so without these definitions every
   `FW_ROLE == PAYLOAD` and `FW_ROLE == GROUND` test would be 0 == 0 and each
   image would compile both roles. */
#define PAYLOAD 1
#define GROUND  0

/* MODEM type: LoRa only */
#define USE_MODEM_LORA  1
#define USE_MODEM_FSK   0

/* NUCLEO-WL55JC2 front-end is the 470 MHz band variant (the upstream example
   targets NUCLEO-WL55JC1 / 868 MHz). Both boards of the link must use the same
   value. Modulation parameters below are left at the upstream defaults so that
   PING/PONG frames stay interoperable with an unmodified example build. */
#define RF_FREQUENCY                                470000000 /* Hz */

#ifndef TX_OUTPUT_POWER   /* please, to change this value, redefine it in USER CODE SECTION */
#define TX_OUTPUT_POWER                             14        /* dBm */
#endif /* TX_OUTPUT_POWER */

#define LORA_BANDWIDTH                              0         /* [0: 125 kHz, 1: 250 kHz, 2: 500 kHz, 3: Reserved] */
#define LORA_SPREADING_FACTOR                       7         /* [SF7..SF12] */
#define LORA_CODINGRATE                             1         /* [1: 4/5, 2: 4/6, 3: 4/7, 4: 4/8] */
#define LORA_PREAMBLE_LENGTH                        8         /* Same for Tx and Rx */
#define LORA_SYMBOL_TIMEOUT                         5         /* Symbols */
#define LORA_FIX_LENGTH_PAYLOAD_ON                  false
#define LORA_IQ_INVERSION_ON                        false

#define PAYLOAD_LEN                                 64

/* PingPong application constants */
#define RX_TIMEOUT_VALUE                            3000
#define TX_TIMEOUT_VALUE                            3000
#define MAX_APP_BUFFER_SIZE                         255
#if (PAYLOAD_LEN > MAX_APP_BUFFER_SIZE)
#error PAYLOAD_LEN must be less or equal than MAX_APP_BUFFER_SIZE
#endif /* (PAYLOAD_LEN > MAX_APP_BUFFER_SIZE) */

/* Wait for remote to be in Rx, before sending a Tx frame */
#define RX_TIME_MARGIN                              200
/* LED blink Period */
#define LED_PERIOD_MS                               200

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