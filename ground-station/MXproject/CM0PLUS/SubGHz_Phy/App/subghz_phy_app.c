/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    subghz_phy_app.c
  * @author  MCD Application Team
  * @brief   Application of the SubGHz_Phy Middleware
  *          Simplified LoRa-only PingPong for PAYLOAD/GROUND roles
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

/* Includes ------------------------------------------------------------------*/
#include "platform.h"
#include "sys_app.h"
#include "subghz_phy_app.h"
#include "radio.h"

/* USER CODE BEGIN Includes */
#include "stm32_timer.h"
#include "stm32_seq.h"
#include "utilities_def.h"
#include "app_version.h"
#include "subghz_phy_version.h"
/* USER CODE END Includes */

/* External variables ---------------------------------------------------------*/
/* USER CODE BEGIN EV */

/* USER CODE END EV */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* Configurations */
/* PingPong transmit interval (ms) */
#define PINGPONG_TX_INTERVAL_MS       5000

/* USER CODE END PD */

/* Private macro ------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* Radio events function pointer */
static RadioEvents_t RadioEvents;

/* USER CODE BEGIN PV */
/* Common */
/* App Rx Buffer */
static uint8_t BufferRx[MAX_APP_BUFFER_SIZE];
/* App Tx Buffer */
static uint8_t BufferTx[MAX_APP_BUFFER_SIZE];
/* Last Received Buffer Size */
static uint16_t RxBufferSize = 0;
/* Last Received packet RSSI */
static int8_t RssiValue = 0;
/* Last Received packet SNR (in LoRa modulation) */
static int8_t SnrValue = 0;

/* Led Timer object */
static UTIL_TIMER_Object_t timerLed;

/* PingPong state machine */
typedef enum
{
  STATE_RX,
  STATE_RX_WAIT,
  STATE_TX,
  STATE_TX_WAIT
} PingPongState_t;

static PingPongState_t State = STATE_RX;

/* PingPong periodic transmit timer */
static UTIL_TIMER_Object_t timerTx;

/* PingPong role: true = Master (initiates PING), false = Slave (responds with PONG) */
static bool IsMaster = true;

/* Packet counter */
static uint16_t PacketCount = 0;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/*!
 * @brief Function to be executed on Radio Tx Done event
 */
static void OnTxDone(void);

/**
  * @brief Function to be executed on Radio Rx Done event
  * @param  payload ptr of buffer received
  * @param  size buffer size
  * @param  rssi
  * @param  LoraSnr_FskCfo
 */
static void OnRxDone(uint8_t *payload, uint16_t size, int16_t rssi, int8_t LoraSnr_FskCfo);

/**
  * @brief Function executed on Radio Tx Timeout event
 */
static void OnTxTimeout(void);

/**
  * @brief Function executed on Radio Rx Timeout event
 */
static void OnRxTimeout(void);

/**
  * @brief Function executed on Radio Rx Error event
 */
static void OnRxError(void);

/* USER CODE BEGIN PFP */
/**
  * @brief  Function executed when LED timer elapses
  * @param  context ptr of LED context
  */
static void OnledEvent(void *context);

/**
  * @brief  PingPong periodic transmit timer callback
  * @param  context unused
  */
static void OnTxTimer(void *context);

/**
  * @brief  PingPong state machine implementation
  */
static void PingPong_Process(void);

/**
  * @brief  Configure radio for TX and send packet
  */
static void RadioSendPacket(uint8_t *buffer, uint16_t size);

/**
  * @brief  Configure radio for continuous RX
  */
static void RadioRxContinuous(void);

/**
  * @brief  Prepare and send PING or PONG packet
  */
static void SendPingPong(void);

/* USER CODE END PFP */

/* Exported functions ---------------------------------------------------------*/
void SubghzApp_Init(void)
{
  /* USER CODE BEGIN SubghzApp_Init_1 */

#if (FW_ROLE == PAYLOAD)
  APP_LOG(TS_OFF, VLEVEL_M, "\n\rLORA PINGPONG (PAYLOAD)\n\r");
#else
  APP_LOG(TS_OFF, VLEVEL_M, "\n\rLORA PINGPONG (GROUND)\n\r");
#endif
  /* Get SubGHY_Phy APP version*/
  APP_LOG(TS_OFF, VLEVEL_M, "APPLICATION_VERSION: V%X.%X.%X\r\n",
          (uint8_t)(APP_VERSION_MAIN),
          (uint8_t)(APP_VERSION_SUB1),
          (uint8_t)(APP_VERSION_SUB2));

  /* Get MW SubGhz_Phy info */
  APP_LOG(TS_OFF, VLEVEL_M, "MW_RADIO_VERSION:    V%X.%X.%X\r\n",
          (uint8_t)(SUBGHZ_PHY_VERSION_MAIN),
          (uint8_t)(SUBGHZ_PHY_VERSION_SUB1),
          (uint8_t)(SUBGHZ_PHY_VERSION_SUB2));

  /* Led Timer */
  UTIL_TIMER_Create(&timerLed, LED_PERIOD_MS, UTIL_TIMER_ONESHOT, OnledEvent, NULL);
  UTIL_TIMER_Start(&timerLed);
  /* USER CODE END SubghzApp_Init_1 */

  /* Radio initialization */
  RadioEvents.TxDone = OnTxDone;
  RadioEvents.RxDone = OnRxDone;
  RadioEvents.TxTimeout = OnTxTimeout;
  RadioEvents.RxTimeout = OnRxTimeout;
  RadioEvents.RxError = OnRxError;

  Radio.Init(&RadioEvents);

  /* USER CODE BEGIN SubghzApp_Init_2 */
  /* Radio Set frequency */
  Radio.SetChannel(RF_FREQUENCY);

  /* Radio configuration */
  APP_LOG(TS_OFF, VLEVEL_M, "---------------\n\r");
  APP_LOG(TS_OFF, VLEVEL_M, "LORA_MODULATION\n\r");
  APP_LOG(TS_OFF, VLEVEL_M, "LORA_BW=%d kHz\n\r", (1 << LORA_BANDWIDTH) * 125);
  APP_LOG(TS_OFF, VLEVEL_M, "LORA_SF=%d\n\r", LORA_SPREADING_FACTOR);

  /* Create PingPong periodic transmit timer */
  UTIL_TIMER_Create(&timerTx, PINGPONG_TX_INTERVAL_MS, UTIL_TIMER_PERIODIC, OnTxTimer, NULL);

  /* Register PingPong process task */
  UTIL_SEQ_RegTask((1 << CFG_SEQ_Task_SubGHz_Phy_App_Process), UTIL_SEQ_RFU, PingPong_Process);

  /* Determine initial role: PAYLOAD starts as Master (sends PING), GROUND starts as Slave (waits for PING) */
#if (FW_ROLE == PAYLOAD)
  IsMaster = true;
  APP_LOG(TS_ON, VLEVEL_M, "Role: MASTER (sends PING)\n\r");
#else
  IsMaster = false;
  APP_LOG(TS_ON, VLEVEL_M, "Role: SLAVE (waits for PING, sends PONG)\n\r");
#endif

  /* Start in RX mode */
  RadioRxContinuous();

  /* If Master, schedule first transmission after a short delay to let peer enter RX */
  if (IsMaster)
  {
    UTIL_TIMER_Start(&timerTx);
    APP_LOG(TS_ON, VLEVEL_M, "TX timer started, interval: %d ms\n\r", PINGPONG_TX_INTERVAL_MS);
  }
  /* USER CODE END SubghzApp_Init_2 */
}

/* USER CODE BEGIN EF */

/* USER CODE END EF */

/* Private functions ---------------------------------------------------------*/
static void OnTxDone(void)
{
  /* USER CODE BEGIN OnTxDone */
  APP_LOG(TS_ON, VLEVEL_L, "OnTxDone\n\r");

  /* Return to RX mode after transmission */
  RadioRxContinuous();

  /* If we were the Master, we just sent a PING - wait for PONG */
  /* If we were the Slave, we just sent a PONG - wait for next PING */
  State = STATE_RX_WAIT;

  /* Run process task */
  UTIL_SEQ_SetTask((1 << CFG_SEQ_Task_SubGHz_Phy_App_Process), CFG_SEQ_Prio_0);
  /* USER CODE END OnTxDone */
}

static void OnRxDone(uint8_t *payload, uint16_t size, int16_t rssi, int8_t LoraSnr_FskCfo)
{
  /* USER CODE BEGIN OnRxDone */
  APP_LOG(TS_ON, VLEVEL_L, "OnRxDone\n\r");
  APP_LOG(TS_ON, VLEVEL_L, "RssiValue=%d dBm, SnrValue=%ddB\n\r", rssi, LoraSnr_FskCfo);
  SnrValue = LoraSnr_FskCfo;

  /* Reject oversize packets to prevent OOB access */
  if (size > MAX_APP_BUFFER_SIZE)
  {
    APP_LOG(TS_ON, VLEVEL_L, "RX: oversize packet %d > %d, ignoring\n\r", size, MAX_APP_BUFFER_SIZE);
    RxBufferSize = 0;
    RadioRxContinuous();
    return;
  }

  /* Clear BufferRx */
  memset(BufferRx, 0, MAX_APP_BUFFER_SIZE);
  /* Record payload size */
  RxBufferSize = size;
  memcpy(BufferRx, payload, RxBufferSize);
  /* Record Received Signal Strength */
  RssiValue = rssi;

  /* Log received packet */
  APP_LOG(TS_ON, VLEVEL_H, "RX: size=%d, RSSI=%d, SNR=%d\n\r", size, rssi, SnrValue);
  for (uint16_t i = 0; i < RxBufferSize; i++)
  {
    APP_LOG(TS_OFF, VLEVEL_H, "%02X", BufferRx[i]);
    if (i % 16 == 15)
    {
      APP_LOG(TS_OFF, VLEVEL_H, "\n\r");
    }
  }
  APP_LOG(TS_OFF, VLEVEL_H, "\n\r");

  /* Run process task to handle received packet */
  UTIL_SEQ_SetTask((1 << CFG_SEQ_Task_SubGHz_Phy_App_Process), CFG_SEQ_Prio_0);
  /* USER CODE END OnRxDone */
}

static void OnTxTimeout(void)
{
  /* USER CODE BEGIN OnTxTimeout */
  APP_LOG(TS_ON, VLEVEL_L, "OnTxTimeout\n\r");
  /* Transmission failed, return to RX */
  RadioRxContinuous();
  State = STATE_RX;
  /* USER CODE END OnTxTimeout */
}

static void OnRxTimeout(void)
{
  /* USER CODE BEGIN OnRxTimeout */
  APP_LOG(TS_ON, VLEVEL_L, "OnRxTimeout\n\r");
  /* RX timeout - if Master, we'll retry on next timer; if Slave, stay in RX */
  RadioRxContinuous();
  State = STATE_RX;
  /* USER CODE END OnRxTimeout */
}

static void OnRxError(void)
{
  /* USER CODE BEGIN OnRxError */
  APP_LOG(TS_ON, VLEVEL_L, "OnRxError\n\r");
  /* RX error - restart RX */
  RadioRxContinuous();
  State = STATE_RX;
  /* USER CODE END OnRxError */
}

/* USER CODE BEGIN PrFD */

static void RadioSendPacket(uint8_t *buffer, uint16_t size)
{
  Radio.Sleep();
  Radio.SetChannel(RF_FREQUENCY);
  Radio.SetTxConfig(MODEM_LORA, TX_OUTPUT_POWER, 0, LORA_BANDWIDTH,
                    LORA_SPREADING_FACTOR, LORA_CODINGRATE,
                    LORA_PREAMBLE_LENGTH, LORA_FIX_LENGTH_PAYLOAD_ON,
                    true, 0, 0, LORA_IQ_INVERSION_ON, TX_TIMEOUT_VALUE);
  Radio.SetMaxPayloadLength(MODEM_LORA, MAX_APP_BUFFER_SIZE);

  Radio.Send(buffer, size);
}

static void RadioRxContinuous(void)
{
  Radio.Sleep();
  Radio.SetChannel(RF_FREQUENCY);
  Radio.SetRxConfig(MODEM_LORA, LORA_BANDWIDTH, LORA_SPREADING_FACTOR,
                    LORA_CODINGRATE, 0, LORA_PREAMBLE_LENGTH,
                    LORA_SYMBOL_TIMEOUT, LORA_FIX_LENGTH_PAYLOAD_ON,
                    0, true, 0, 0, LORA_IQ_INVERSION_ON, true);
  if (LORA_FIX_LENGTH_PAYLOAD_ON == true)
  {
    Radio.SetMaxPayloadLength(MODEM_LORA, PAYLOAD_LEN);
  }
  else
  {
    Radio.SetMaxPayloadLength(MODEM_LORA, MAX_APP_BUFFER_SIZE);
  }

  /* Continuous RX: timeout 0 means stay in RX until packet received */
  Radio.Rx(0);
}

static void SendPingPong(void)
{
  /* Prepare packet: "PING" or "PONG" followed by counter and padding */
  memset(BufferTx, 0, PAYLOAD_LEN);

  if (IsMaster)
  {
    memcpy(BufferTx, "PING", 4);
  }
  else
  {
    memcpy(BufferTx, "PONG", 4);
  }

  /* Add packet counter (2 bytes, little-endian) */
  BufferTx[4] = (uint8_t)(PacketCount & 0xFF);
  BufferTx[5] = (uint8_t)((PacketCount >> 8) & 0xFF);

  /* Increment counter for next packet */
  PacketCount++;

  /* Send packet */
  RadioSendPacket(BufferTx, PAYLOAD_LEN);
  State = STATE_TX_WAIT;
}

static void PingPong_Process(void)
{
  switch (State)
  {
    case STATE_RX:
      /* Already in RX mode, nothing to do */
      break;

    case STATE_RX_WAIT:
      /* Just returned to RX after TX, wait for packet */
      break;

    case STATE_TX:
      /* Time to send a packet */
      SendPingPong();
      break;

    case STATE_TX_WAIT:
      /* Waiting for OnTxDone or OnTxTimeout callback */
      break;

    default:
      State = STATE_RX;
      break;
  }

  /* Process received packet if any */
  if (RxBufferSize > 0)
  {
    /* Check if received packet is PING or PONG */
    /* Require >= 6 bytes: 4 for "PING"/"PONG" + 2 for counter */
    bool is_ping = (RxBufferSize >= 6) && (memcmp(BufferRx, "PING", 4) == 0);
    bool is_pong = (RxBufferSize >= 6) && (memcmp(BufferRx, "PONG", 4) == 0);

    if (is_ping)
    {
      APP_LOG(TS_ON, VLEVEL_M, "RX: PING received (cnt=%d)\n\r",
              (BufferRx[5] << 8) | BufferRx[4]);

      if (!IsMaster)
      {
        /* We are Slave, respond with PONG */
        IsMaster = false;  /* Remain slave */
        State = STATE_TX;
        UTIL_SEQ_SetTask((1 << CFG_SEQ_Task_SubGHz_Phy_App_Process), CFG_SEQ_Prio_0);
      }
      else
      {
        /* We are Master but received PING - other side also thinks it's Master */
        /* Stay Master, ignore */
        APP_LOG(TS_ON, VLEVEL_L, "RX: Unexpected PING while Master\n\r");
      }
    }
    else if (is_pong)
    {
      APP_LOG(TS_ON, VLEVEL_M, "RX: PONG received (cnt=%d)\n\r",
              (BufferRx[5] << 8) | BufferRx[4]);

      if (IsMaster)
      {
        /* We are Master, received PONG response - good, wait for next interval */
        State = STATE_RX;
      }
      else
      {
        /* We are Slave but received PONG - other side also thinks it's Slave */
        /* Stay Slave, ignore */
        APP_LOG(TS_ON, VLEVEL_L, "RX: Unexpected PONG while Slave\n\r");
      }
    }
    else
    {
      APP_LOG(TS_ON, VLEVEL_L, "RX: Unknown packet type\n\r");
    }

    RxBufferSize = 0;
  }
}

static void OnTxTimer(void *context)
{
  (void)context;
  /* Periodic timer fired - Master sends next PING */
  if (IsMaster && (State == STATE_RX || State == STATE_RX_WAIT))
  {
    State = STATE_TX;
    UTIL_SEQ_SetTask((1 << CFG_SEQ_Task_SubGHz_Phy_App_Process), CFG_SEQ_Prio_0);
  }
  /* If Slave, timer is ignored (Slave only responds to PING) */
}

static void OnledEvent(void *context)
{
  HAL_GPIO_TogglePin(LED2_GPIO_Port, LED2_Pin); /* LED_GREEN */
  HAL_GPIO_TogglePin(LED3_GPIO_Port, LED3_Pin); /* LED_RED */
  UTIL_TIMER_Start(&timerLed);
}

/* USER CODE END PrFD */