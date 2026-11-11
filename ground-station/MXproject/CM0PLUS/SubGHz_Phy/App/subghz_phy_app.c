/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    subghz_phy_app.c
  * @author  MCD Application Team
  * @brief   Application of the SubGHz_Phy Middleware
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

#if (FW_ROLE == PAYLOAD)
#include "gnss_status.h"
#include "telemetry.h"
#endif
#if (FW_ROLE == GROUND)
#include "telemetry.h"
#endif
/* USER CODE END Includes */

/* External variables ---------------------------------------------------------*/
/* USER CODE BEGIN EV */

/* USER CODE END EV */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
/* PingPong states removed - replaced by TelemetryState_t */
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* Configurations */
/* Timeout */
#define RX_TIMEOUT_VALUE              3000
#define TX_TIMEOUT_VALUE              3000
/* Size of the payload to be sent */
#define MAX_APP_BUFFER_SIZE          255
#if (PAYLOAD_LEN > MAX_APP_BUFFER_SIZE)
#error PAYLOAD_LEN must be less or equal than MAX_APP_BUFFER_SIZE
#endif /* (PAYLOAD_LEN > MAX_APP_BUFFER_SIZE) */
/* Wait for remote to be in Rx, before sending a Tx frame */
#define RX_TIME_MARGIN                200
/* Afc bandwidth in Hz */
#define FSK_AFC_BANDWIDTH             83333
/* LED blink Period */
#define LED_PERIOD_MS                 200

#if (FW_ROLE == PAYLOAD)
/* Telemetry transmit interval (ms) */
#define TELEMETRY_TX_INTERVAL_MS      10000
/* Maximum time to wait for ACK after TX (ms) */
#define ACK_TIMEOUT_MS                2000
#endif

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

#if (FW_ROLE == PAYLOAD)
/* Telemetry FSM states */
typedef enum
{
  TELEMETRY_STATE_TX,
  TELEMETRY_STATE_TX_IN_PROGRESS,
  TELEMETRY_STATE_RX_ACK,
  TELEMETRY_STATE_WAIT_INTERVAL
} TelemetryState_t;

static TelemetryState_t TelemetryState = TELEMETRY_STATE_WAIT_INTERVAL;

/* Telemetry periodic transmit timer */
static UTIL_TIMER_Object_t timerTelemetryTx;
/* ACK wait timeout timer */
static UTIL_TIMER_Object_t timerAckTimeout;

/* Telemetry packet sequence number */
static uint16_t telemetry_sequence = 0;

/* Pending packet buffer for ACK wait (exact copy of what was transmitted) */
static uint8_t pending_tx_buffer[TELEMETRY_PACKET_SIZE];
static uint16_t pending_tx_len = 0;
static uint16_t pending_sequence = 0;

/* Retransmission state */
static uint8_t retry_count = 0;
#define MAX_RETRIES                   2u  /* 2 retries = 3 total attempts */
#endif /* FW_ROLE == PAYLOAD */

#if (FW_ROLE == GROUND)
/* Ground station state */
/* Last acknowledged sequence number for duplicate detection */
static uint16_t last_rx_sequence = 0;
static bool     last_rx_sequence_valid = false;
#endif /* FW_ROLE == GROUND */

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

#if (FW_ROLE == PAYLOAD)
/**
  * @brief  Telemetry periodic transmit timer callback
  * @param  context unused
  */
static void OnTelemetryTxTimer(void *context);

/**
  * @brief  ACK wait timeout timer callback
  * @param  context unused
  */
static void OnAckTimeoutTimer(void *context);

/**
  * @brief  Telemetry state machine implementation
  */
static void Telemetry_Process(void);

/**
  * @brief  Configure radio for TX and send telemetry packet
  */
static void RadioSendTelemetry(void);

/**
  * @brief  Configure radio for RX (ACK wait or continuous)
  */
static void RadioRxContinuous(void);

/**
  * @brief  Build and queue the next telemetry packet
  * @return true if packet was built and transmitted, false otherwise
  */
static bool Telemetry_BuildAndTransmit(void);
#endif /* FW_ROLE == PAYLOAD */

#if (FW_ROLE == GROUND)
/**
  * @brief  Ground station continuous RX process
  */
static void GroundRx_Process(void);

/**
  * @brief  Configure radio for continuous RX
  */
static void RadioRxContinuous(void);

/**
  * @brief  Build and send ACK packet for a given sequence
  */
static void RadioSendAck(uint16_t sequence);
#endif /* FW_ROLE == GROUND */

/* USER CODE END PFP */

/* Exported functions ---------------------------------------------------------*/
void SubghzApp_Init(void)
{
  /* USER CODE BEGIN SubghzApp_Init_1 */

#if (FW_ROLE == PAYLOAD)
  APP_LOG(TS_OFF, VLEVEL_M, "\n\rTELEMETRY APP (PAYLOAD)\n\r");
#else
  APP_LOG(TS_OFF, VLEVEL_M, "\n\rGROUND STATION (RX ONLY)\n\r");
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
#if ((USE_MODEM_LORA == 1) && (USE_MODEM_FSK == 0))
  APP_LOG(TS_OFF, VLEVEL_M, "---------------\n\r");
  APP_LOG(TS_OFF, VLEVEL_M, "LORA_MODULATION\n\r");
  APP_LOG(TS_OFF, VLEVEL_M, "LORA_BW=%d kHz\n\r", (1 << LORA_BANDWIDTH) * 125);
  APP_LOG(TS_OFF, VLEVEL_M, "LORA_SF=%d\n\r", LORA_SPREADING_FACTOR);
#elif ((USE_MODEM_LORA == 0) && (USE_MODEM_FSK == 1))
  APP_LOG(TS_OFF, VLEVEL_M, "---------------\n\r");
  APP_LOG(TS_OFF, VLEVEL_M, "FSK_MODULATION\n\r");
  APP_LOG(TS_OFF, VLEVEL_M, "FSK_BW=%d Hz\n\r", FSK_BANDWIDTH);
  APP_LOG(TS_OFF, VLEVEL_M, "FSK_DR=%d bits/s\n\r", FSK_DATARATE);
#else
#error "Please define a modulation in the subghz_phy_app.h file."
#endif /* USE_MODEM_LORA | USE_MODEM_FSK */

#if (FW_ROLE == PAYLOAD)
  /* Create telemetry periodic transmit timer (10s period) */
  UTIL_TIMER_Create(&timerTelemetryTx, TELEMETRY_TX_INTERVAL_MS, UTIL_TIMER_PERIODIC, OnTelemetryTxTimer, NULL);
  /* Create ACK timeout timer (one-shot) */
  UTIL_TIMER_Create(&timerAckTimeout, ACK_TIMEOUT_MS, UTIL_TIMER_ONESHOT, OnAckTimeoutTimer, NULL);

  APP_LOG(TS_ON, VLEVEL_L, "Telemetry interval: %d ms\n\r", TELEMETRY_TX_INTERVAL_MS);
  APP_LOG(TS_ON, VLEVEL_L, "ACK timeout: %d ms\n\r", ACK_TIMEOUT_MS);

  /* Register telemetry process task */
  UTIL_SEQ_RegTask((1 << CFG_SEQ_Task_SubGHz_Phy_App_Process), UTIL_SEQ_RFU, Telemetry_Process);

  /* Start periodic transmission timer */
  UTIL_TIMER_Start(&timerTelemetryTx);
  /* The periodic timer's first expiry is one interval away: request the first
     packet now so that it is sent as soon as the sequencer runs. */
  TelemetryState = TELEMETRY_STATE_TX;
  UTIL_SEQ_SetTask((1 << CFG_SEQ_Task_SubGHz_Phy_App_Process), CFG_SEQ_Prio_0);
#else
  /* Ground role: continuous RX only (no PingPong) */
  APP_LOG(TS_OFF, VLEVEL_M, "GROUND STATION (RX + ACK)\n\r");
  /* Register ground RX process task */
  UTIL_SEQ_RegTask((1 << CFG_SEQ_Task_SubGHz_Phy_App_Process), UTIL_SEQ_RFU, GroundRx_Process);

  /* Start in continuous RX mode */
  RadioRxContinuous();
#endif
  /* USER CODE END SubghzApp_Init_2 */
}

/* USER CODE BEGIN EF */

/* USER CODE END EF */

/* Private functions ---------------------------------------------------------*/
static void OnTxDone(void)
{
  /* USER CODE BEGIN OnTxDone */
  APP_LOG(TS_ON, VLEVEL_L, "OnTxDone\n\r");

#if (FW_ROLE == PAYLOAD)
  /* TX actually completed - now safe to enter RX_ACK and start timer */
  if (TelemetryState == TELEMETRY_STATE_TX_IN_PROGRESS)
  {
    TelemetryState = TELEMETRY_STATE_RX_ACK;
    /* Start ACK wait timer */
    UTIL_TIMER_Start(&timerAckTimeout);
    /* Switch to RX mode to await ACK */
    RadioRxContinuous();
  }
  else
  {
    APP_LOG(TS_ON, VLEVEL_L, "OnTxDone: unexpected state %d\n\r", TelemetryState);
  }
#else
  /* Ground role: ACK transmission completed, return to continuous RX */
  RadioRxContinuous();
#endif
  /* USER CODE END OnTxDone */
}

static void OnRxDone(uint8_t *payload, uint16_t size, int16_t rssi, int8_t LoraSnr_FskCfo)
{
  /* USER CODE BEGIN OnRxDone */
  APP_LOG(TS_ON, VLEVEL_L, "OnRxDone\n\r");
#if ((USE_MODEM_LORA == 1) && (USE_MODEM_FSK == 0))
  APP_LOG(TS_ON, VLEVEL_L, "RssiValue=%d dBm, SnrValue=%ddB\n\r", rssi, LoraSnr_FskCfo);
  SnrValue = LoraSnr_FskCfo;
#endif /* USE_MODEM_LORA | USE_MODEM_FSK */
#if ((USE_MODEM_LORA == 0) && (USE_MODEM_FSK == 1))
  APP_LOG(TS_ON, VLEVEL_L, "RssiValue=%d dBm, Cfo=%dkHz\n\r", rssi, LoraSnr_FskCfo);
  SnrValue = 0;
#endif /* USE_MODEM_LORA | USE_MODEM_FSK */

  /* Reject oversize packets to prevent OOB access */
  if (size > MAX_APP_BUFFER_SIZE)
  {
    APP_LOG(TS_ON, VLEVEL_L, "RX: oversize packet %d > %d, ignoring\n\r", size, MAX_APP_BUFFER_SIZE);
    RxBufferSize = 0;
    /* Stay in RX */
#if (FW_ROLE == PAYLOAD)
    if (TelemetryState == TELEMETRY_STATE_RX_ACK || TelemetryState == TELEMETRY_STATE_WAIT_INTERVAL)
    {
      RadioRxContinuous();
    }
#else
    RadioRxContinuous();
#endif
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
#if (FW_ROLE == PAYLOAD)
  /* TX failed - if we have retries left, retry; otherwise give up */
  if (TelemetryState == TELEMETRY_STATE_TX_IN_PROGRESS)
  {
    if (retry_count < MAX_RETRIES)
    {
      retry_count++;
      APP_LOG(TS_ON, VLEVEL_L, "TX timeout: retry %u/%u for seq=%u\n\r",
              retry_count, MAX_RETRIES, pending_sequence);
      /* Retransmit the exact same pending packet */
      memcpy(BufferTx, pending_tx_buffer, pending_tx_len);
      RadioSendTelemetry();
      /* State remains TX_IN_PROGRESS - OnTxDone or OnTxTimeout will handle next step */
    }
    else
    {
      /* Exhausted retries */
      APP_LOG(TS_ON, VLEVEL_L, "TX: exhausted retries for seq=%u, logging unacknowledged\n\r", pending_sequence);
      pending_tx_len = 0;
      pending_sequence = 0;
      retry_count = 0;
      TelemetryState = TELEMETRY_STATE_WAIT_INTERVAL;
      UTIL_SEQ_SetTask((1 << CFG_SEQ_Task_SubGHz_Phy_App_Process), CFG_SEQ_Prio_0);
    }
  }
#else
  /* Ground role: ACK transmission failed, return to continuous RX */
  RadioRxContinuous();
#endif
  /* USER CODE END OnTxTimeout */
}

static void OnRxTimeout(void)
{
  /* USER CODE BEGIN OnRxTimeout */
  APP_LOG(TS_ON, VLEVEL_L, "OnRxTimeout\n\r");
#if (FW_ROLE == PAYLOAD)
  /* RX timeout while waiting for ACK - will be handled by ACK timeout timer */
  /* Stay in RX_ACK state until ACK timeout fires */
  if (TelemetryState == TELEMETRY_STATE_RX_ACK)
  {
    RadioRxContinuous();
  }
  else if (TelemetryState == TELEMETRY_STATE_WAIT_INTERVAL)
  {
    RadioRxContinuous();
  }
#else
  /* Ground role: continuous RX */
  RadioRxContinuous();
#endif
  /* USER CODE END OnRxTimeout */
}

static void OnRxError(void)
{
  /* USER CODE BEGIN OnRxError */
  APP_LOG(TS_ON, VLEVEL_L, "OnRxError\n\r");
#if (FW_ROLE == PAYLOAD)
  /* RX error while waiting for ACK - restart RX */
  if (TelemetryState == TELEMETRY_STATE_RX_ACK ||
      TelemetryState == TELEMETRY_STATE_WAIT_INTERVAL)
  {
    RadioRxContinuous();
  }
#else
  /* Ground role: continuous RX */
  RadioRxContinuous();
#endif
  /* USER CODE END OnRxError */
}

/* USER CODE BEGIN PrFD */

#if (FW_ROLE == PAYLOAD)
static void RadioSendTelemetry(void)
{
#if ((USE_MODEM_LORA == 1) && (USE_MODEM_FSK == 0))
  Radio.Sleep();
  Radio.SetChannel(RF_FREQUENCY);
  Radio.SetTxConfig(MODEM_LORA, TX_OUTPUT_POWER, 0, LORA_BANDWIDTH,
                    LORA_SPREADING_FACTOR, LORA_CODINGRATE,
                    LORA_PREAMBLE_LENGTH, LORA_FIX_LENGTH_PAYLOAD_ON,
                    true, 0, 0, LORA_IQ_INVERSION_ON, TX_TIMEOUT_VALUE);
  Radio.SetMaxPayloadLength(MODEM_LORA, MAX_APP_BUFFER_SIZE);
#elif ((USE_MODEM_LORA == 0) && (USE_MODEM_FSK == 1))
  Radio.SetTxConfig(MODEM_FSK, TX_OUTPUT_POWER, FSK_FDEV, 0,
                    FSK_DATARATE, 0,
                    FSK_PREAMBLE_LENGTH, FSK_FIX_LENGTH_PAYLOAD_ON,
                    true, 0, 0, 0, TX_TIMEOUT_VALUE);
  Radio.SetMaxPayloadLength(MODEM_FSK, MAX_APP_BUFFER_SIZE);
#else
#error "Please define a modulation in the subghz_phy_app.h file."
#endif /* USE_MODEM_LORA | USE_MODEM_FSK */

  Radio.Send(BufferTx, pending_tx_len);
}

static void RadioRxContinuous(void)
{
#if ((USE_MODEM_LORA == 1) && (USE_MODEM_FSK == 0))
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
#elif ((USE_MODEM_LORA == 0) && (USE_MODEM_FSK == 1))
  Radio.SetRxConfig(MODEM_FSK, FSK_BANDWIDTH, FSK_DATARATE,
                    0, FSK_AFC_BANDWIDTH, FSK_PREAMBLE_LENGTH,
                    0, FSK_FIX_LENGTH_PAYLOAD_ON, 0, true,
                    0, 0, false, true);
  if (LORA_FIX_LENGTH_PAYLOAD_ON == true)
  {
    Radio.SetMaxPayloadLength(MODEM_FSK, PAYLOAD_LEN);
  }
  else
  {
    Radio.SetMaxPayloadLength(MODEM_FSK, MAX_APP_BUFFER_SIZE);
  }
#else
#error "Please define a modulation in the subghz_phy_app.h file."
#endif /* USE_MODEM_LORA | USE_MODEM_FSK */

  /* Continuous RX: timeout 0 means stay in RX until packet received */
  Radio.Rx(0);
}

static bool Telemetry_BuildAndTransmit(void)
{
  TelemetryGpsStatus gps_status;
  bool has_fix;

  /* Drop anything received before this packet existed: only a packet sent
     after this point can acknowledge it, so a leftover ACK (or a packet of
     another transmitter) must not be matched against the new sequence. */
  RxBufferSize = 0;

  /* Read latest GPS status from shared memory */
  has_fix = GNSS_STATUS_ReadLatest(&gps_status);

  /* Encode telemetry packet */
  if (!Telemetry_EncodeGpsStatus(&gps_status, telemetry_sequence, BufferTx, MAX_APP_BUFFER_SIZE))
  {
    APP_LOG(TS_ON, VLEVEL_L, "TX: encode failed\n\r");
    return false;
  }

  pending_tx_len = TELEMETRY_PACKET_SIZE;
  pending_sequence = telemetry_sequence;
  retry_count = 0;  /* Reset retry counter for new packet */

  /* Store copy of transmitted packet for potential ACK matching */
  memcpy(pending_tx_buffer, BufferTx, pending_tx_len);

  /* Log transmission */
  if (has_fix)
  {
    APP_LOG(TS_ON, VLEVEL_L, "TX: seq=%u FIX lat=%ld lon=%ld alt=%ld sats=%u\n\r",
            telemetry_sequence,
            gps_status.latitude_udeg,
            gps_status.longitude_udeg,
            gps_status.altitude_cm,
            gps_status.satellites_used);
  }
  else
  {
    APP_LOG(TS_ON, VLEVEL_L, "TX: seq=%u NO_FIX\n\r", telemetry_sequence);
  }

  /* Increment sequence for next packet */
  telemetry_sequence++;

  /* Transmit */
  RadioSendTelemetry();
  return true;
}

static void Telemetry_Process(void)
{
  switch (TelemetryState)
  {
    case TELEMETRY_STATE_TX:
      /* Build and transmit telemetry packet */
      if (!Telemetry_BuildAndTransmit())
      {
        /* Transmission setup failed, wait for next interval */
        TelemetryState = TELEMETRY_STATE_WAIT_INTERVAL;
      }
      else
      {
        /* Transmission started, OnTxDone will transition to RX_ACK */
        TelemetryState = TELEMETRY_STATE_TX_IN_PROGRESS;
      }
      break;

    case TELEMETRY_STATE_TX_IN_PROGRESS:
      /* Waiting for OnTxDone or OnTxTimeout callback */
      /* Do nothing here - callbacks handle transitions */
      break;

    case TELEMETRY_STATE_RX_ACK:
      /* Waiting for ACK */
      if (RxBufferSize > 0)
      {
        uint16_t ack_sequence = 0;
        bool is_ack = Telemetry_DecodeAck(BufferRx, RxBufferSize, &ack_sequence);
        RxBufferSize = 0;

        if (is_ack && ack_sequence == pending_sequence)
        {
          /* Valid ACK for our pending packet */
          APP_LOG(TS_ON, VLEVEL_L, "ACK: received for seq=%u\n\r", ack_sequence);
          UTIL_TIMER_Stop(&timerAckTimeout);
          pending_tx_len = 0;
          pending_sequence = 0;
          TelemetryState = TELEMETRY_STATE_WAIT_INTERVAL;
          /* Enter continuous RX until next TX */
          RadioRxContinuous();
        }
        else
        {
          /* Not a valid ACK for our packet - ignore, keep waiting */
          if (is_ack)
          {
            APP_LOG(TS_ON, VLEVEL_L, "ACK: ignored mismatched seq=%u (expected %u)\n\r",
                    ack_sequence, pending_sequence);
          }
          else
          {
            APP_LOG(TS_ON, VLEVEL_L, "RX: ignored non-ACK packet in RX_ACK\n\r");
          }
          RadioRxContinuous();
        }
      }
      break;

    case TELEMETRY_STATE_WAIT_INTERVAL:
      /* Waiting for next periodic timer trigger */
      /* Radio is in continuous RX mode */
      break;

    default:
      TelemetryState = TELEMETRY_STATE_WAIT_INTERVAL;
      break;
  }
}

static void OnTelemetryTxTimer(void *context)
{
  (void)context;
  /* Periodic timer fired - time to send next telemetry packet */
  if (TelemetryState == TELEMETRY_STATE_WAIT_INTERVAL)
  {
    TelemetryState = TELEMETRY_STATE_TX;
    UTIL_SEQ_SetTask((1 << CFG_SEQ_Task_SubGHz_Phy_App_Process), CFG_SEQ_Prio_0);
  }
  else
  {
    /* Previous transmission still pending (ACK wait or TX in progress) */
    /* Log but don't transmit - the interval timer is periodic so we'll catch the next one */
    APP_LOG(TS_ON, VLEVEL_L, "TX timer: busy, skipping interval\n\r");
  }
}

static void OnAckTimeoutTimer(void *context)
{
  (void)context;
  /* ACK timeout expired */
  if (TelemetryState == TELEMETRY_STATE_RX_ACK)
  {
    APP_LOG(TS_ON, VLEVEL_L, "ACK: timeout for seq=%u (attempt %u/%u)\n\r",
            pending_sequence, retry_count + 1, MAX_RETRIES + 1);
    if (retry_count < MAX_RETRIES)
    {
      retry_count++;
      APP_LOG(TS_ON, VLEVEL_L, "RETRY: attempt %u for seq=%u\n\r", retry_count, pending_sequence);
      /* Retransmit the exact same pending packet */
      memcpy(BufferTx, pending_tx_buffer, pending_tx_len);
      TelemetryState = TELEMETRY_STATE_TX_IN_PROGRESS;
      RadioSendTelemetry();
      /* OnTxDone will start new ACK timer and enter RX_ACK */
    }
    else
    {
      /* Exhausted all attempts */
      APP_LOG(TS_ON, VLEVEL_L, "ACK: exhausted %u attempts for seq=%u, logging unacknowledged\n\r",
              MAX_RETRIES + 1, pending_sequence);
      pending_tx_len = 0;
      pending_sequence = 0;
      retry_count = 0;
      TelemetryState = TELEMETRY_STATE_WAIT_INTERVAL;
      /* Enter continuous RX until next TX */
      RadioRxContinuous();
    }
  }
}
#else
/* Ground role: continuous RX + ACK */
static void RadioRxContinuous(void)
{
#if ((USE_MODEM_LORA == 1) && (USE_MODEM_FSK == 0))
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
#elif ((USE_MODEM_LORA == 0) && (USE_MODEM_FSK == 1))
  Radio.SetRxConfig(MODEM_FSK, FSK_BANDWIDTH, FSK_DATARATE,
                    0, FSK_AFC_BANDWIDTH, FSK_PREAMBLE_LENGTH,
                    0, FSK_FIX_LENGTH_PAYLOAD_ON, 0, true,
                    0, 0, false, true);
  if (LORA_FIX_LENGTH_PAYLOAD_ON == true)
  {
    Radio.SetMaxPayloadLength(MODEM_FSK, PAYLOAD_LEN);
  }
  else
  {
    Radio.SetMaxPayloadLength(MODEM_FSK, MAX_APP_BUFFER_SIZE);
  }
#else
#error "Please define a modulation in the subghz_phy_app.h file."
#endif /* USE_MODEM_LORA | USE_MODEM_FSK */

  /* Continuous RX: timeout 0 means stay in RX until packet received */
  Radio.Rx(0);
}

static void RadioSendAck(uint16_t sequence)
{
#if ((USE_MODEM_LORA == 1) && (USE_MODEM_FSK == 0))
  Radio.Sleep();
  Radio.SetChannel(RF_FREQUENCY);
  Radio.SetTxConfig(MODEM_LORA, TX_OUTPUT_POWER, 0, LORA_BANDWIDTH,
                    LORA_SPREADING_FACTOR, LORA_CODINGRATE,
                    LORA_PREAMBLE_LENGTH, LORA_FIX_LENGTH_PAYLOAD_ON,
                    true, 0, 0, LORA_IQ_INVERSION_ON, TX_TIMEOUT_VALUE);
  Radio.SetMaxPayloadLength(MODEM_LORA, MAX_APP_BUFFER_SIZE);
#elif ((USE_MODEM_LORA == 0) && (USE_MODEM_FSK == 1))
  Radio.SetTxConfig(MODEM_FSK, TX_OUTPUT_POWER, FSK_FDEV, 0,
                    FSK_DATARATE, 0,
                    FSK_PREAMBLE_LENGTH, FSK_FIX_LENGTH_PAYLOAD_ON,
                    true, 0, 0, 0, TX_TIMEOUT_VALUE);
  Radio.SetMaxPayloadLength(MODEM_FSK, MAX_APP_BUFFER_SIZE);
#else
#error "Please define a modulation in the subghz_phy_app.h file."
#endif /* USE_MODEM_LORA | USE_MODEM_FSK */

  /* Encode ACK packet */
  if (!Telemetry_EncodeAck(sequence, BufferTx, MAX_APP_BUFFER_SIZE))
  {
    APP_LOG(TS_ON, VLEVEL_L, "ACK: encode failed for seq=%u\n\r", sequence);
    return;
  }

  /* Transmit ACK */
  Radio.Send(BufferTx, TELEMETRY_ACK_PACKET_SIZE);
}

static void GroundRx_Process(void)
{
  /* Process received packet if any */
  if (RxBufferSize == 0)
  {
    return;
  }

  uint16_t rx_sequence = 0;
  TelemetryGpsStatus gps_status;
  bool is_telemetry = Telemetry_DecodeGpsStatus(BufferRx, RxBufferSize, &gps_status, &rx_sequence);
  RxBufferSize = 0;

  if (!is_telemetry)
  {
    /* Not a valid telemetry packet - ignore, stay in RX */
    APP_LOG(TS_ON, VLEVEL_L, "RX: invalid telemetry packet, ignoring\n\r");
    RadioRxContinuous();
    return;
  }

  /* Valid telemetry packet received */
  bool is_duplicate = (last_rx_sequence_valid && rx_sequence == last_rx_sequence);

  /* Log the packet (only if not a duplicate) */
  if (!is_duplicate)
  {
    if (gps_status.fix_valid != 0u)
    {
      APP_LOG(TS_ON, VLEVEL_L, "RX: seq=%u FIX lat=%ld lon=%ld alt=%ld sats=%u RSSI=%d SNR=%d\n\r",
              rx_sequence,
              gps_status.latitude_udeg,
              gps_status.longitude_udeg,
              gps_status.altitude_cm,
              gps_status.satellites_used,
              RssiValue,
              SnrValue);
    }
    else
    {
      APP_LOG(TS_ON, VLEVEL_L, "RX: seq=%u NO_FIX RSSI=%d SNR=%d\n\r",
              rx_sequence,
              RssiValue,
              SnrValue);
    }
    last_rx_sequence = rx_sequence;
    last_rx_sequence_valid = true;
  }
  else
  {
    APP_LOG(TS_ON, VLEVEL_L, "RX: seq=%u DUPLICATE (ACK only)\n\r", rx_sequence);
  }

  /* Always send ACK for valid packets (including duplicates) */
  RadioSendAck(rx_sequence);
  /* OnTxDone will return to RX */
}
#endif

static void OnledEvent(void *context)
{
  HAL_GPIO_TogglePin(LED2_GPIO_Port, LED2_Pin); /* LED_GREEN */
  HAL_GPIO_TogglePin(LED3_GPIO_Port, LED3_Pin); /* LED_RED */
  UTIL_TIMER_Start(&timerLed);
}

/* USER CODE END PrFD */