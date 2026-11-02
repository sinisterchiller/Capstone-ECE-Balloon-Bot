/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    subghz_phy_app.c
  * @author  MCD Application Team
  * @brief   Application of the SubGHz_Phy Middleware
  *
  *          Adapted from ST's SubGHz_Phy_PingPong example.
  *          Implements PING/PONG protocol with:
  *            - Sequence numbers (0-255, wraps)
  *            - Payload data (echoed in PONG)
  *            - Length validation (rejects truncated/overlong/empty)
  *            - RSSI/SNR reporting on UART
  *            - TX done/timeout and RX timeout/error recovery
  *            - Event-driven FSM with one-shot callbacks
  *          All TX paths guarded by compile-time APP_TX_ENABLED.
  *
  *          Event-driven state machine design:
  *            Radio callbacks set AppState (one-shot state transition)
  *            and set radio_rearm_pending when a new RX arm is needed.
  *            PingPong_Process() is scheduled via UTIL_SEQ_SetTask and
  *            consumes the radio_rearm_pending flag exactly once to
  *            call RadioRx() — never repeatedly on timer-only wakes.
  *
  *            APP_STATE_RX              → Listening (default)
  *            APP_STATE_TX_PING         → Initiator transmitting PING
  *            APP_STATE_WAIT_PONG       → Initiator listening for PONG
  *            APP_STATE_TX_PONG         → Responder transmitting PONG
  *
  *            Flow (initiator):
  *              RX → (one-shot timer fires) → TX_PING →
  *                (OnTxDone) → WAIT_PONG, re-arm RX →
  *                  (OnRxDone with PONG) → consume PONG, re-arm RX → RX
  *                  (OnRxTimeout) → clear pending, re-arm RX → RX
  *                  (OnRxError) → clear pending, re-arm RX → RX
  *
  *            Flow (responder):
  *              RX → (OnRxDone with PING) → TX_PONG →
  *                (OnTxDone) → RX, re-arm RX
  *
  *            Key invariants:
  *              - RadioRx() called only when radio_rearm_pending is set
  *              - No RadioRx() during active TX (checked by AppState)
  *              - No RadioRx() on timer-only wakes (flag not set)
  *              - One-shot ping timer; re-armed after each exchange
  *              - pending_ping.valid guards PING send and PONG matching
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
#include "app_build_config.h"  /* APP_ROLE_INITIATOR, APP_TX_ENABLED — single-source config */
#include "radio.h"

/* USER CODE BEGIN Includes */
#include "stm32_timer.h"
#include "stm32_seq.h"
#include "utilities_def.h"
#include "app_version.h"
#include "subghz_phy_version.h"
#include "usart_if.h"
#include "stm32wlxx_hal_usart.h"  /* for huart2 extern — DiagMarker blocking USART2 write */
#include <string.h>
/* USER CODE END Includes */

/* Startup diagnostic marker — prints via blocking USART2 write before UTIL_ADV_TRACE_Init.
 * Uses HAL_UART_Transmit with HAL_MAX_DELAY (blocking) on huart2, which is already
 * initialized by MX_USART2_UART_Init.  HAL_MAX_DELAY is safe here because HAL_GetTick
 * is called only inside HAL_UART_Transmit's timeout loop, and the call blocks until
 * completion rather than relying on a finite tick count.
 * Usage: DIAG_MARKER("label text") — does not depend on RTC/timers. */
extern UART_HandleTypeDef huart2;
static void DiagMarker(const char *msg)
{
  HAL_UART_Transmit(&huart2, (uint8_t *)msg, (uint16_t)strlen(msg), HAL_MAX_DELAY);
  HAL_UART_Transmit(&huart2, (uint8_t *)"\r\n", 2, HAL_MAX_DELAY);
}

/* External variables ---------------------------------------------------------*/
/* USER CODE BEGIN EV */

/* USER CODE END EV */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* Configurations */
/*Timeout*/
#define RX_TIMEOUT_VALUE              3000
#define TX_TIMEOUT_VALUE              3000
/*Size of the application buffer */
#define MAX_APP_BUFFER_SIZE          255
#if (PAYLOAD_LEN > MAX_APP_BUFFER_SIZE)
#error PAYLOAD_LEN must be less or equal than MAX_APP_BUFFER_SIZE
#endif /* (PAYLOAD_LEN > MAX_APP_BUFFER_SIZE) */
/* wait for remote to be in Rx, before sending a Tx frame*/
#define RX_TIME_MARGIN                200
/* Afc bandwidth in Hz */
#define FSK_AFC_BANDWIDTH             83333
/* LED blink Period*/
#define LED_PERIOD_MS                 200

/* Message protocol constants */
#define MSG_SYNC_WORD         0xA0U
#define MSG_HEADER_LEN        6U
#define MSG_MIN_FRAME_LEN     7U        /* minimum: header(6) + 1-byte payload */
#define MSG_MAX_FRAME_LEN     64U       /* maximum valid frame */
#define MSG_MAX_LEN           64U       /* alias for MsgSerialize compatibility */
#define MSG_MAX_PAYLOAD       58U       /* maximum payload bytes */
#define MSG_MIN_PAYLOAD       1U        /* minimum payload bytes (non-empty) */

/* Message types */
#define MSG_TYPE_PING         0x01U
#define MSG_TYPE_PONG         0x02U

/* Board ID: derived from APP_ROLE_INITIATOR */
#define BOARD_ID              ((APP_ROLE_INITIATOR != 0U) ? ((uint8_t)0x01U) : ((uint8_t)0x02U))

/* PING/PONG payload strings */
#define PING_STR              "PING"
#define PONG_STR              "PONG"

/* Fixed test payload — short, printable, distinct from PING/PONG strings.
 * Responder echoes the exact bytes back in its PONG.
 * To change, update the string below — TEST_PAYLOAD_LEN is derived
 * automatically from sizeof(TEST_PAYLOAD)-1 (excludes null terminator).
 * Compile-time assert enforces 1..58 bytes. */
#define TEST_PAYLOAD          "LORA"
#define TEST_PAYLOAD_LEN      ((uint8_t)(sizeof(TEST_PAYLOAD) - 1))

/* Compile-time validation: TEST_PAYLOAD_LEN must be 1..58 */
extern char test_payload_len_check[(TEST_PAYLOAD_LEN >= 1U) && (TEST_PAYLOAD_LEN <= 58U) ? 1 : -1];

/* Cycle delay between PING sends (ms).
 * One-shot ping timer is re-armed after each exchange with this value. */
#define APP_CYCLE_DELAY_MS    2000

/* Interval to schedule next PING after a completed exchange (ms).
 * ~2× the PING period gives the responder time to reply and the initiator
 * time to process the PONG before the next timer fires. */
#define PING_NEXT_INTERVAL_MS (APP_CYCLE_DELAY_MS * 2)

/* UART trace helper — minimal output for RX events */
#define APP_TRACE(str)        vcom_Trace((uint8_t *)(str), (uint16_t)strlen(str))
#define APP_TRACE_INT(val) do { \
  char _buf[16]; uint8_t _i = 0; \
  int32_t _v = (val); \
  if (_v < 0) { vcom_Trace((uint8_t*)"-", 1); _v = -_v; } \
  if (_v == 0) { vcom_Trace((uint8_t*)"0", 1); } \
  else { while (_v > 0 && _i < 15) { _buf[_i++] = '0' + (_v % 10); _v /= 10; } \
         for (uint8_t _j = _i; _j > 0; _j--) vcom_Trace((uint8_t*)&_buf[_j-1], 1); } } while(0)
#define APP_TRACE_HEX_BYTE(b) do { \
  static const char _hx[] = "0123456789ABCDEF"; \
  vcom_Trace((uint8_t*)&_hx[((b)>>4)&0x0F], 1); \
  vcom_Trace((uint8_t*)&_hx[(b)&0x0F], 1); } while(0)

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* Radio events function pointer */
static RadioEvents_t RadioEvents;

/* USER CODE BEGIN PV */
/* App Rx Buffer*/
static uint8_t BufferRx[MAX_APP_BUFFER_SIZE];
/* App Tx Buffer*/
static uint8_t BufferTx[MAX_APP_BUFFER_SIZE];
/* Last  Received Buffer Size*/
static uint16_t RxBufferSize = 0;
/* Last  Received packet RSSI*/
static int8_t RssiValue = 0;
/* Last  Received packet SNR (in Lora modulation)*/
static int8_t SnrValue = 0;
/* Led Timers objects*/
static UTIL_TIMER_Object_t timerLed;
/* random delay to make sure 2 devices will sync*/
static int32_t random_delay;

/* Sequence counter for outgoing messages (PING only) */
static uint8_t tx_seq = 0;

/* One-shot PING timer (ST timer).  Re-armed after each exchange or timeout.
 * Not free-running — prevents spurious wakes during active TX or WAIT_PONG. */
static UTIL_TIMER_Object_t timerPingOneShot;

/* Pending PING info — tracked so PONG can echo exact seq/payload.
 * pending_ping.valid is set when a PING is sent and cleared when the
 * matching PONG arrives, on timeout, or on error. */
typedef struct
{
  uint8_t  seq;                     /* sequence number of the outstanding PING */
  uint8_t  payload[MSG_MAX_PAYLOAD];/* payload echoed back in PONG */
  uint8_t  len;                     /* payload length (1..58) */
  bool     valid;                   /* true when there is a pending PING to match */
} PendingPing_t;

static PendingPing_t pending_ping = { 0, {0}, 0, false };

/* Counters for diagnostics */
static uint32_t tx_count = 0;
static uint32_t rx_count = 0;
static uint32_t error_count = 0;

/* FSM state — driven by radio callbacks, never by self-scheduling.
 *
 * Radio callbacks (OnTxDone, OnRxTimeout, OnRxError, OnRxDone) set AppState
 * to the target state and schedule PingPong_Process via
 * UTIL_SEQ_SetTask.  PingPong_Process consumes the state transition
 * and re-arms RX only when explicitly requested via
 * radio_rearm_pending flag.
 *
 * APP_STATE_RX:            Listening for incoming messages.
 * APP_STATE_TX_PING:       Initiator is transmitting PING.
 * APP_STATE_WAIT_PONG:     Initiator is listening for PONG response.
 * APP_STATE_TX_PONG:       Responder is transmitting PONG echo.
 *
 * RadioRx() is called only when radio_rearm_pending is set — an
 * explicit one-shot event flag set by callbacks that require a new
 * RX arm.  Timer-only wakes never re-arm RX.
 */

/* Set by PingOneShotTimerCallback to request a PING send.
 * PingPong_Process consumes it in STEP 2. */
static volatile uint8_t ping_pending = 0;

/* Explicit one-shot event flag: set by callbacks that need a new RX arm,
 * consumed/cleared in PingPong_Process() before calling RadioRx().
 * Timer-only wakes do NOT set this flag.  State alone never triggers RX. */
static volatile uint8_t radio_rearm_pending = 0;

/* Set by OnRxDone to signal that RX data has been buffered and is ready
 * for PingPong_Process() to handle before re-arming RX. */
static volatile uint8_t rx_processing_complete = 0;

typedef enum
{
  APP_STATE_RX,             /* Waiting for RX (default) */
  APP_STATE_TX_PING,        /* TX in progress (initiator sent PING) */
  APP_STATE_WAIT_PONG,      /* TX done, waiting for PONG echo */
  APP_STATE_TX_PONG,        /* TX in progress (responder sent PONG) */
} AppState_t;

static AppState_t AppState = APP_STATE_RX;

/* USER CODE END PV */

/* Message structure for protocol framing */
typedef struct __attribute__((packed)) {
  uint8_t sync;           /* 0xA0 */
  uint8_t seq;
  uint8_t sender_id;
  uint8_t msg_type;
  uint8_t len;
  uint8_t reserved;
  uint8_t payload[58];
} LoRaMessage_t;

/* Private function prototypes -----------------------------------------------*/
/*!
 * \brief Function to be executed on Radio Tx Done event
 */
static void OnTxDone(void);

/*!
 * \brief Function executed on Radio Rx Done event
 * \param  payload ptr of buffer received
 * \param  size  received frame size in bytes (uint16_t from driver)
 * \param  rssi
 * \param  LoraSnr_FskCfo
 */
static void OnRxDone(uint8_t *payload, uint16_t size, int16_t rssi, int8_t LoraSnr_FskCfo);

/*!
 * \brief Function executed on Radio Tx Timeout event
 */
static void OnTxTimeout(void);

/*!
 * \brief Function executed on Radio Rx Timeout event
 */
static void OnRxTimeout(void);

/*!
 * \brief Function executed on Radio Rx Error event
 */
static void OnRxError(void);

/* USER CODE BEGIN PFP */
/**
  * @brief  Function executed on when led timer elapses
  * @param  context ptr of LED context
  */
static void OnledEvent(void *context);

/**
  * @brief  One-shot PING timer callback — fires when it's time to send next PING.
  *         Sets ping_pending flag; PingPong_Process consumes it.
  *         Only the initiator will actually send (gated by APP_ROLE_INITIATOR).
  *         Fires the PingPong_Process handler via UTIL_SEQ_SetTask.
  * @param  context unused
  */
static void PingOneShotTimerCallback(void *context);

/**
  * @brief PingPong state machine implementation.
  *
  *        Called by radio callbacks (OnTxDone, OnRxDone, OnTxTimeout,
  *        OnRxTimeout, OnRxError) and by the one-shot PING timer.
  *        Driven entirely by events — never self-schedules.
  *        Returns immediately after any Radio.Send() to avoid aborting TX.
  *        RadioRx() is called only when radio_rearm_pending event flag
  *        is set — an explicit one-shot request from callbacks.
  *        Timer-only wakes never set this flag and never re-arm RX.
  */
static void PingPong_Process(void);

/**
  * @brief Centralized TX helper — configures radio and sends with given length.
  *        All TX paths go through this function.
  * @param  buf  pointer to serialized buffer
  * @param  len  actual serialized length (7..64)
  */
static void RadioSendWithLength(uint8_t *buf, uint8_t len);

/**
  * @brief Enter RX mode (called after init, TX done, timeout, error)
  */
static void RadioRx(void);

/* Internal helpers */
static void MsgInit(LoRaMessage_t *msg, uint8_t sender, uint8_t type,
                    const uint8_t *payload, uint8_t len);
static uint8_t MsgSerialize(const LoRaMessage_t *msg, uint8_t *buf);
static uint8_t MsgDeserialize(const uint8_t *buf, uint8_t len, LoRaMessage_t *msg);
static void UartPrintStatus(void);

/* USER CODE END PFP */

/* Exported functions ---------------------------------------------------------*/
void SubghzApp_Init(void)
{
  /* USER CODE BEGIN SubghzApp_Init_1 */

  DiagMarker("DIAG: SubghzApp_Init START");

  /* Replace early APP_LOG calls with blocking UART markers.
   * These startup APP_LOG calls were replaced to isolate a suspected
   * advanced-trace issue observed during boot; the exact root cause
   * is not yet confirmed.
   * Version strings are omitted here — the main banner in main.c
   * already reports role, frequency, and TX state. */
  DiagMarker("DIAG: APP_LOG replaced");

  /* Led Timers */
  DiagMarker("DIAG: timerLed Create START");
  UTIL_TIMER_Create(&timerLed, LED_PERIOD_MS, UTIL_TIMER_ONESHOT, OnledEvent, NULL);
  DiagMarker("DIAG: timerLed Create OK");
  DiagMarker("DIAG: timerLed Start START");
  UTIL_TIMER_Start(&timerLed);
  DiagMarker("DIAG: timerLed Start OK");

  /* One-shot PING timer — created but only started by initiator.
   * Re-armed after each exchange or timeout (never free-running). */
  DiagMarker("DIAG: timerPingOneShot Create START");
  UTIL_TIMER_Create(&timerPingOneShot, APP_CYCLE_DELAY_MS, UTIL_TIMER_ONESHOT,
                    PingOneShotTimerCallback, NULL);
  DiagMarker("DIAG: timerPingOneShot Create OK");

  DiagMarker("DIAG: SubghzApp_Init timers OK");

  /* USER CODE END SubghzApp_Init_1 */

  /* Radio initialization */
  RadioEvents.TxDone = OnTxDone;
  RadioEvents.RxDone = OnRxDone;
  RadioEvents.TxTimeout = OnTxTimeout;
  RadioEvents.RxTimeout = OnRxTimeout;
  RadioEvents.RxError = OnRxError;

  DiagMarker("DIAG: Radio.Init");
  Radio.Init(&RadioEvents);
  DiagMarker("DIAG: Radio.Init OK");

  /* USER CODE BEGIN SubghzApp_Init_2 */
  /*calculate random delay for synchronization*/
  DiagMarker("DIAG: Radio.Random");
  random_delay = (Radio.Random()) >> 22; /*10bits random e.g. from 0 to 1023 ms*/
  DiagMarker("DIAG: Radio.Random OK");

  /* Radio Set frequency */
  DiagMarker("DIAG: Radio.SetChannel START");
  Radio.SetChannel(RF_FREQUENCY);
  DiagMarker("DIAG: Radio.SetChannel OK");

  /* Modulation config — startup-only, replaced APP_LOG (advanced-trace hang risk)
   * with blocking UART markers to isolate the suspected
   * APP_LOG hang observed after "DIAG: Radio.Random OK". */
  DiagMarker("DIAG: Modulation config (LORA)");

  /*fills tx buffer*/
  memset(BufferTx, 0x0, MAX_APP_BUFFER_SIZE);
  DiagMarker("DIAG: BufferTx memset OK");

  /* Replaced APP_LOG (advanced-trace hang) with blocking UART marker.
   * random_delay value omitted — main banner in main.c reports it. */
  DiagMarker("DIAG: random_delay skipped (APP_LOG replaced)");

  /*register task to be run in while(1) after Radio IT*/
  DiagMarker("DIAG: UTIL_SEQ_RegTask START");
  UTIL_SEQ_RegTask((1 << CFG_SEQ_Task_SubGHz_Phy_App_Process), UTIL_SEQ_RFU, PingPong_Process);
  DiagMarker("DIAG: UTIL_SEQ_RegTask OK");

  /* NOTE: Removed unnecessary HAL_Delay(RX_TIMEOUT_VALUE + random_delay).
   * The initiator's one-shot PING timer already provides the startup delay.
   * RX can be armed immediately — no blocking wait required. */

  /* Enter RX mode immediately after init */
  DiagMarker("DIAG: RadioRx");
  RadioRx();
  DiagMarker("DIAG: RadioRx OK");

  /* Start one-shot PING timer only for initiator role */
#if (APP_ROLE_INITIATOR != 0)
  DiagMarker("DIAG: timerPingOneShot Start START");
  UTIL_TIMER_Start(&timerPingOneShot);
  DiagMarker("DIAG: timerPingOneShot Start OK");
#endif

  DiagMarker("DIAG: SubghzApp_Init OK");

  /* USER CODE END SubghzApp_Init_2 */
}

/* USER CODE BEGIN EF */

/* USER CODE END EF */

/* Private functions ---------------------------------------------------------*/
static void OnTxDone(void)
{
  /* USER CODE BEGIN OnTxDone */
  APP_LOG(TS_ON, VLEVEL_L, "OnTxDone\n\r");

  tx_count++;

#if (APP_ROLE_INITIATOR != 0)
  /* Initiator: PING TX complete — transition to waiting for PONG. */
  AppState = APP_STATE_WAIT_PONG;
#else
  /* Responder: PONG TX complete — return to RX. */
  AppState = APP_STATE_RX;
#endif
  /* Each TxDone arms exactly one RX via the event flag. */
  radio_rearm_pending = 1;
  /* Schedule PingPong_Process to handle the state transition */
  UTIL_SEQ_SetTask((1 << CFG_SEQ_Task_SubGHz_Phy_App_Process), CFG_SEQ_Prio_0);
  /* USER CODE END OnTxDone */
}

static void OnRxDone(uint8_t *payload, uint16_t size, int16_t rssi, int8_t LoraSnr_FskCfo)
{
  /* USER CODE BEGIN OnRxDone */
  APP_LOG(TS_ON, VLEVEL_L, "OnRxDone\n\r");

#if ((USE_MODEM_LORA == 1) && (USE_MODEM_FSK == 0))
  APP_LOG(TS_ON, VLEVEL_L, "RssiValue=%d dBm, SnrValue=%ddB\n\r", rssi, LoraSnr_FskCfo);
  /* Record payload Signal to noise ratio in Lora*/
  SnrValue = LoraSnr_FskCfo;
#endif /* USE_MODEM_LORA | USE_MODEM_FSK */
#if ((USE_MODEM_LORA == 0) && (USE_MODEM_FSK == 1))
  APP_LOG(TS_ON, VLEVEL_L, "RssiValue=%d dBm, Cfo=%dkHz\n\r", rssi, LoraSnr_FskCfo);
  SnrValue = 0; /*not applicable in GFSK*/
#endif /* USE_MODEM_LORA | USE_MODEM_FSK */

  /* Validate received frame size before narrowing uint16_t -> uint8_t.
   * Per protocol: minimum 7 bytes (6-byte header + 1-byte payload),
   * maximum 64 bytes. Reject out-of-range sizes to prevent reading
   * uninitialized buffer data. */
  if (size < MSG_MIN_FRAME_LEN || size > MSG_MAX_FRAME_LEN)
  {
    error_count++;
    /* Schedule process to handle invalid-size frame and re-arm RX */
    RxBufferSize = 0;
    memset(BufferRx, 0, MAX_APP_BUFFER_SIZE);
    RssiValue = rssi;
    rx_processing_complete = 1;
    UTIL_SEQ_SetTask((1 << CFG_SEQ_Task_SubGHz_Phy_App_Process), CFG_SEQ_Prio_0);
    return;
  }

  /* Clear BufferRx */
  memset(BufferRx, 0, MAX_APP_BUFFER_SIZE);
  /* Record payload size — safe: validated 7..64 fits in uint8_t */
  RxBufferSize = (uint8_t)size;
  memcpy(BufferRx, payload, RxBufferSize);
  /* Record Received Signal Strength */
  RssiValue = rssi;
  /* Record payload content */
  APP_LOG(TS_ON, VLEVEL_H, "payload. size=%d \n\r", size);
  for (int32_t i = 0; i < (int32_t)RxBufferSize && i < PAYLOAD_LEN; i++)
  {
    APP_LOG(TS_OFF, VLEVEL_H, "%02X", BufferRx[i]);
    if (i % 16 == 15)
    {
      APP_LOG(TS_OFF, VLEVEL_H, "\n\r");
    }
  }
  APP_LOG(TS_OFF, VLEVEL_H, "\n\r");
  /* Signal that RX data is ready for processing. */
  rx_processing_complete = 1;
  /* Schedule PingPong_Process to handle the received data */
  UTIL_SEQ_SetTask((1 << CFG_SEQ_Task_SubGHz_Phy_App_Process), CFG_SEQ_Prio_0);
  /* USER CODE END OnRxDone */
}

static void OnTxTimeout(void)
{
  /* USER CODE BEGIN OnTxTimeout */
  APP_LOG(TS_ON, VLEVEL_L, "OnTxTimeout\n\r");

  error_count++;
  /* Clear pending PING — TX failed. */
  pending_ping.valid = false;
  /* Transition to RX recovery. */
  AppState = APP_STATE_RX;
  radio_rearm_pending = 1;
  UTIL_SEQ_SetTask((1 << CFG_SEQ_Task_SubGHz_Phy_App_Process), CFG_SEQ_Prio_0);
  /* Initiator schedules next one-shot ping if TX is enabled. */
#if (APP_ROLE_INITIATOR != 0)
#if APP_TX_ENABLED != 0
  UTIL_TIMER_SetPeriod(&timerPingOneShot, APP_CYCLE_DELAY_MS);
  UTIL_TIMER_Start(&timerPingOneShot);
#endif
#endif
  /* USER CODE END OnTxTimeout */
}

static void OnRxTimeout(void)
{
  /* USER CODE BEGIN OnRxTimeout */
  APP_LOG(TS_ON, VLEVEL_L, "OnRxTimeout\n\r");

  error_count++;
  /* If waiting for PONG, clear pending and schedule next one-shot ping. */
  if (AppState == APP_STATE_WAIT_PONG)
  {
    pending_ping.valid = false;
#if (APP_ROLE_INITIATOR != 0)
#if APP_TX_ENABLED != 0
    UTIL_TIMER_SetPeriod(&timerPingOneShot, APP_CYCLE_DELAY_MS);
    UTIL_TIMER_Start(&timerPingOneShot);
#endif
#endif
  }
  /* Transition to RX — both initiator and responder re-arm. */
  AppState = APP_STATE_RX;
  radio_rearm_pending = 1;
  UTIL_SEQ_SetTask((1 << CFG_SEQ_Task_SubGHz_Phy_App_Process), CFG_SEQ_Prio_0);
  /* USER CODE END OnRxTimeout */
}

static void OnRxError(void)
{
  /* USER CODE BEGIN OnRxError */
  APP_LOG(TS_ON, VLEVEL_L, "OnRxError\n\r");

  error_count++;
  /* If waiting for PONG, clear pending and schedule next one-shot ping. */
  if (AppState == APP_STATE_WAIT_PONG)
  {
    pending_ping.valid = false;
#if (APP_ROLE_INITIATOR != 0)
#if APP_TX_ENABLED != 0
    UTIL_TIMER_SetPeriod(&timerPingOneShot, APP_CYCLE_DELAY_MS);
    UTIL_TIMER_Start(&timerPingOneShot);
#endif
#endif
  }
  /* Transition to RX recovery — both initiator and responder re-arm. */
  AppState = APP_STATE_RX;
  radio_rearm_pending = 1;
  UTIL_SEQ_SetTask((1 << CFG_SEQ_Task_SubGHz_Phy_App_Process), CFG_SEQ_Prio_0);
  /* USER CODE END OnRxError */
}

/* Internal helpers */
static void MsgInit(LoRaMessage_t *msg, uint8_t sender, uint8_t type,
                    const uint8_t *payload, uint8_t len)
{
  memset(msg, 0, sizeof(LoRaMessage_t));
  msg->sync = MSG_SYNC_WORD;
  msg->seq = 0;  /* seq set by caller, not here */
  msg->sender_id = sender;
  msg->msg_type = type;
  msg->len = (len > sizeof(msg->payload)) ? (uint8_t)sizeof(msg->payload) : len;
  if ((payload != NULL) && (len > 0U))
  {
    memcpy(msg->payload, payload, msg->len);
  }
}

static uint8_t MsgSerialize(const LoRaMessage_t *msg, uint8_t *buf)
{
  uint8_t total;
  if ((msg == NULL) || (buf == NULL)) return 0U;
  total = MSG_HEADER_LEN + msg->len;
  if (total > MSG_MAX_LEN) total = MSG_MAX_LEN;
  buf[0] = msg->sync;
  buf[1] = msg->seq;
  buf[2] = msg->sender_id;
  buf[3] = msg->msg_type;
  buf[4] = msg->len;
  buf[5] = msg->reserved;
  if (msg->len > 0U)
  {
    memcpy(&buf[MSG_HEADER_LEN], msg->payload, msg->len);
  }
  return total;
}

/**
  * @brief  Deserialize and validate a received message.
  *
  *         Validates:
  *           - frame_length in [7, 64]  (header(6) + min payload(1))
  *           - declared payload length in [1, 58] (non-empty)
  *           - frame_length == 6 + declared_length
  *           - sync word == 0xA0
  *           - msg_type is PING or PONG
  *           - reserved byte is 0x00
  *
  *         Returns 1U on success, 0U on any validation failure.
  */
static uint8_t MsgDeserialize(const uint8_t *buf, uint8_t len, LoRaMessage_t *msg)
{
  /* Null checks */
  if ((buf == NULL) || (msg == NULL)) return 0U;

  /* Reject if too short for header + payload (min 7 bytes) */
  if (len < MSG_MIN_FRAME_LEN) return 0U;

  /* Reject if frame too long */
  if (len > MSG_MAX_FRAME_LEN) return 0U;

  /* Validate sync word */
  if (buf[0] != MSG_SYNC_WORD) return 0U;

  /* Read header fields */
  msg->sync = buf[0];
  msg->seq = buf[1];
  msg->sender_id = buf[2];
  msg->msg_type = buf[3];
  msg->len = buf[4];
  msg->reserved = buf[5];

  /* Validate declared payload length: must be 1..58 (non-empty) */
  if (msg->len < MSG_MIN_PAYLOAD) return 0U;
  if (msg->len > MSG_MAX_PAYLOAD) return 0U;

  /* Validate frame_length == 6 + declared_length */
  uint8_t expected_frame = (uint8_t)(MSG_HEADER_LEN + msg->len);
  if (len != expected_frame) return 0U;

  /* Validate msg_type */
  if ((msg->msg_type != MSG_TYPE_PING) && (msg->msg_type != MSG_TYPE_PONG))
    return 0U;

  /* Validate reserved byte */
  if (msg->reserved != 0x00) return 0U;

  /* Copy payload (safe: len == expected_frame, so we have exactly msg->len bytes) */
  if (msg->len > 0U)
  {
    memcpy(msg->payload, &buf[MSG_HEADER_LEN], msg->len);
  }

  return 1U;
}

static void UartPrintStatus(void)
{
  APP_TRACE("  === Status ===\r\n");
  APP_TRACE("  TX: "); APP_TRACE_INT((int32_t)tx_count); APP_TRACE("\r\n");
  APP_TRACE("  RX: "); APP_TRACE_INT((int32_t)rx_count); APP_TRACE("\r\n");
  APP_TRACE("  ERR: "); APP_TRACE_INT((int32_t)error_count); APP_TRACE("\r\n");
}

/**
  * @brief  Centralized TX helper.
  *
  *         All TX paths go through this function. It configures the radio
  *         for transmission and calls Radio.Send() with the actual serialized
  *         length. The APP_TX_ENABLED guard is inside this function so that
  *         source-level TX safety is centralized — no Radio.Send code is
  *         compiled when APP_TX_ENABLED==0, regardless of caller.
  *
  * @param  buf  pointer to serialized buffer (7..64 bytes)
  * @param  len  actual serialized length
  */
static void RadioSendWithLength(uint8_t *buf, uint8_t len)
{
#if APP_TX_ENABLED != 0
  /* DIAG: Radio state before TX config */
  APP_TRACE("[TX-SEND] pre_state=");
  APP_TRACE_INT((int)Radio.GetStatus());
  APP_TRACE(" role=");
  APP_TRACE((APP_ROLE_INITIATOR != 0) ? "INIT" : "RESP");
  APP_TRACE(" len=");
  APP_TRACE_INT((int32_t)len);
  APP_TRACE("\r\n");

  Radio.Sleep();
  Radio.SetChannel(RF_FREQUENCY);
#if ((USE_MODEM_LORA == 1) && (USE_MODEM_FSK == 0))
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

  /* DIAG: Radio state right before Send() */
  APP_TRACE("[TX-SEND] pre_send=");
  APP_TRACE_INT((int)Radio.GetStatus());
  APP_TRACE("\r\n");

  Radio.Send(buf, len);
#else
  /* TX disabled by compile-time configuration — log and return.
   * No state transition, no Radio.Send, no code emitted. */
  APP_TRACE("  [TX DISABLED] RadioSendWithLength called, len=");
  APP_TRACE_INT((int32_t)len);
  APP_TRACE("\r\n");
  (void)buf;  /* suppress unused-parameter warning */
#endif /* APP_TX_ENABLED */
}

static void RadioRx(void)
{
  /* DIAG: Before Sleep/Configure — show radio state entering RadioRx() */
  RadioState_t pre_state = Radio.GetStatus();
  APP_TRACE("[RX-ARM] pre_state=");
  APP_TRACE_INT((int)pre_state);
  APP_TRACE(" role=");
  APP_TRACE((APP_ROLE_INITIATOR != 0) ? "INIT" : "RESP");
  APP_TRACE("\r\n");

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

  /* DIAG: After SetRxConfig, before Rx() — confirm radio is idle before arming */
  APP_TRACE("[RX-ARM] post_config=");
  APP_TRACE_INT((int)Radio.GetStatus());
  APP_TRACE("\r\n");

  Radio.Rx(RX_TIMEOUT_VALUE);

  /* DIAG: After Rx() — confirm radio is now in RX_RUNNING */
  APP_TRACE("[RX-ARM] post_rx=");
  APP_TRACE_INT((int)Radio.GetStatus());
  APP_TRACE("\r\n");
}

/**
  * @brief  One-shot PING timer callback.
  *
  *         Fires once when the one-shot timer expires.  Sets ping_pending
  *         flag to request a PING send.  PingPong_Process consumes the flag
  *         and re-arms the timer for the next exchange.
  *         Only the initiator role will actually send PINGs (gated by
  *         APP_ROLE_INITIATOR in PingPong_Process).
  * @param  context unused
  */
static void PingOneShotTimerCallback(void *context)
{
  (void)context;
  /* Set pending flag so PingPong_Process will send a PING */
  ping_pending = 1;
  /* Wake the sequencer to process periodic PING */
  UTIL_SEQ_SetTask((1 << CFG_SEQ_Task_SubGHz_Phy_App_Process), CFG_SEQ_Prio_0);
}

static void PingPong_Process(void)
{
  /* =========================================================================
   * ENTRY — Consume rx_processing_complete flag set by OnRxDone.
   *
   * OnRxDone sets rx_processing_complete=1 (for both valid and invalid-size
   * frames) and schedules PingPong_Process via UTIL_SEQ_SetTask.  Consume
   * the flag here and set radio_rearm_pending so the flag-consumption block
   * at the bottom calls RadioRx() exactly once.
   *
   * Guard against TX-active states: if a PONG TX was started, OnTxDone
   * will handle re-arming RX after TX completes.
   * ========================================================================= */
  if (rx_processing_complete && AppState != APP_STATE_TX_PING && AppState != APP_STATE_TX_PONG)
  {
    rx_processing_complete = 0;
    radio_rearm_pending = 1;
  }

  /* =========================================================================
   * STEP 1 — RX HANDLING (only when OnRxDone delivered data).
   *
   * OnRxDone may have populated BufferRx/RxBufferSize with a valid frame
   * (or left RxBufferSize=0 for an invalid-size frame rejected in
   * OnRxDone).  We process the buffered frame BEFORE re-arming RX.
   * ========================================================================= */
  if (RxBufferSize > 0)
  {
    rx_count++;

    /* Deserialize and validate message */
    LoRaMessage_t rx_msg;
    if (MsgDeserialize((const uint8_t *)BufferRx, (uint8_t)RxBufferSize, &rx_msg))
    {
      const char *type_str;
      switch (rx_msg.msg_type)
      {
        case MSG_TYPE_PING:  type_str = "PING"; break;
        case MSG_TYPE_PONG:  type_str = "PONG"; break;
        default:             type_str = "UNKWN"; break;
      }

      APP_TRACE("  [RX#"); APP_TRACE_INT((int32_t)rx_count);
      APP_TRACE("] seq="); APP_TRACE_INT((int32_t)rx_msg.seq);
      APP_TRACE(" from=0x"); APP_TRACE_HEX_BYTE(rx_msg.sender_id);
      APP_TRACE(" type="); APP_TRACE(type_str);
      APP_TRACE(" RSSI="); APP_TRACE_INT((int32_t)RssiValue);
      APP_TRACE(" SNR="); APP_TRACE_INT((int32_t)SnrValue);
      APP_TRACE("\r\n");

      if (rx_msg.len > 0)
      {
        uint8_t print_len = rx_msg.len;  /* Print all validated payload bytes (1..58) */
        APP_TRACE("  DATA: ");
        for (uint8_t i = 0; i < print_len; i++)
        {
          APP_TRACE_HEX_BYTE((uint8_t)rx_msg.payload[i]);
          APP_TRACE(" ");
        }
        APP_TRACE("\r\n");
      }

      /* =========================================================================
       * RESPONDER: echo PING as PONG
       * Only responder (APP_ROLE_INITIATOR==0) handles PING from initiator.
       * Validates sender is the initiator (0x01).
       * PONG echoes the received PING's seq and payload exactly.
       * ========================================================================= */
      if ((rx_msg.msg_type == MSG_TYPE_PING) &&
          (rx_msg.sender_id == 0x01U) &&
          (APP_ROLE_INITIATOR == 0U))
      {
        LoRaMessage_t pong_msg;
        /* Use the received PING's seq and payload for the PONG echo */
        MsgInit(&pong_msg, BOARD_ID, MSG_TYPE_PONG,
                rx_msg.payload, rx_msg.len);
        pong_msg.seq = rx_msg.seq;  /* echo the PING's sequence number */

#if APP_TX_ENABLED != 0
        {
          uint8_t tx_buf[MSG_MAX_LEN];
          uint8_t total = MsgSerialize(&pong_msg, tx_buf);
          /* Centralized TX through RadioSendWithLength */
          RadioSendWithLength(tx_buf, total);
          AppState = APP_STATE_TX_PONG;
          /* Clear received buffer — don't reuse stale data */
          RxBufferSize = 0;
          /* Clear any stale rearm flag that could abort TX.
           * OnTxDone will set radio_rearm_pending=1 to re-arm RX. */
          radio_rearm_pending = 0;
          /* Return immediately — do NOT fall through to RadioRx() below.
           * OnTxDone will schedule PingPong_Process to return to RX. */
          return;
        }
#else
        APP_TRACE("  [TX DISABLED] not transmitting PONG\r\n");
        /* TX disabled — stay in current state, keep listening */
#endif
      }
      /* =========================================================================
       * INITIATOR: received PONG — validate against outstanding PING
       * Only initiator (APP_ROLE_INITIATOR!=0) accepts PONG.
       * PONG must match sender (0x02 = responder), type, sequence, and payload
       * of pending PING.
       * ========================================================================= */
      else if (rx_msg.msg_type == MSG_TYPE_PONG)
      {
#if (APP_ROLE_INITIATOR != 0)
        /* Check if this PONG matches our outstanding PING */
        if (pending_ping.valid &&
            rx_msg.sender_id == 0x02U &&
            rx_msg.msg_type == MSG_TYPE_PONG &&
            rx_msg.seq == pending_ping.seq &&
            rx_msg.len == pending_ping.len &&
            memcmp(rx_msg.payload, pending_ping.payload, pending_ping.len) == 0)
        {
          APP_TRACE("  [PONG MATCHED] ack seq=");
          APP_TRACE_INT((int32_t)rx_msg.seq);
          APP_TRACE("\r\n");
          /* Clear pending PING — PONG matched */
          pending_ping.valid = false;
          /* Return to RX state so the fall-through re-arms the radio.
           * Without this, AppState stays at WAIT_PONG and the rearm
           * guard (AppState == APP_STATE_RX) below would skip RadioRx(). */
          AppState = APP_STATE_RX;
          /* Schedule next PING after a sensible interval */
          UTIL_TIMER_SetPeriod(&timerPingOneShot, PING_NEXT_INTERVAL_MS);
          UTIL_TIMER_Start(&timerPingOneShot);
        }
        else
        {
          APP_TRACE("  [PONG MISMATCH] ignored\r\n");
          /* Mismatched/unrelated PONG: DO NOT clear pending;
           * continue WAIT_PONG.  Re-arm RX below. */
        }
#else
        /* Responder received a PONG (shouldn't happen normally) */
        APP_TRACE("  [PONG UNEXPECTED] ignored on responder\r\n");
#endif
      }
      /* Unknown type — ignore */
      else
      {
        APP_TRACE("  [UNKNOWN TYPE] ignored\r\n");
      }
    }
    else
    {
      APP_TRACE("  [RX#"); APP_TRACE_INT((int32_t)rx_count);
      APP_TRACE("] BAD PKT len="); APP_TRACE_INT((int32_t)RxBufferSize);
      APP_TRACE(" RSSI="); APP_TRACE_INT((int32_t)RssiValue);
      APP_TRACE("\r\n");
      error_count++;
    }
    /* Clear received buffer — don't reuse stale data */
    RxBufferSize = 0;

    /* After handling RX (valid or invalid payload), set re-arm flag.
     * If AppState != APP_STATE_RX a PONG TX was started above;
     * OnTxDone will re-arm RX after TX completes.
     *
     * FALL THROUGH to the flag-consumption block below (same call, no return)
     * so that radio_rearm_pending is consumed and RadioRx() is called here
     * rather than waiting for the next scheduled process invocation. */
    if (AppState == APP_STATE_RX)
    {
      radio_rearm_pending = 1;
    }
    /* No return — fall through to STEP 2 (periodic PING) and STEP 3 (RX rearm) */
  }

  /* =========================================================================
   * STEP 2 — PERIODIC PING SEND (initiator only).
   *
   * Only fires when PingOneShotTimerCallback has set ping_pending flag.
   * Guard: must be in APP_STATE_RX (idle, not transmitting, not waiting)
   * and no outstanding PING (one-shot ping semantics).
   * ========================================================================= */
#if (APP_ROLE_INITIATOR != 0)
  if (ping_pending && (AppState == APP_STATE_RX) && !pending_ping.valid)
  {
    /* Consume the timer flag */
    ping_pending = 0;

    APP_TRACE("  [TX] Sending PING seq=");
    APP_TRACE_INT((int32_t)tx_seq);
    APP_TRACE("\r\n");

#if APP_TX_ENABLED != 0
    {
      uint8_t tx_buf[MSG_MAX_LEN];
      LoRaMessage_t ping_msg;
      MsgInit(&ping_msg, BOARD_ID, MSG_TYPE_PING,
              (const uint8_t *)TEST_PAYLOAD, TEST_PAYLOAD_LEN);
      ping_msg.seq = tx_seq;  /* set sequence number here */

      uint8_t total = MsgSerialize(&ping_msg, tx_buf);
      /* Centralized TX through RadioSendWithLength */
      RadioSendWithLength(tx_buf, total);
      /* Track pending PING so we can match the PONG echo */
      pending_ping.seq = tx_seq;
      pending_ping.len = TEST_PAYLOAD_LEN;
      memcpy(pending_ping.payload, (const uint8_t *)TEST_PAYLOAD, pending_ping.len);
      pending_ping.valid = true;
      /* Advance sequence for next PING */
      tx_seq++;
      /* Clear any stale rearm flag before starting TX.
       * OnTxDone will set radio_rearm_pending=1 to transition to WAIT_PONG. */
      radio_rearm_pending = 0;
      AppState = APP_STATE_TX_PING;
      /* Return immediately — do NOT fall through to RadioRx() below.
       * OnTxDone will schedule PingPong_Process to transition to
       * APP_STATE_WAIT_PONG. */
      return;
    }
#else
    APP_TRACE("  [TX DISABLED] not transmitting PING\r\n");
    /* TX disabled — consume flag but do not advance sequence or track pending.
     * Restart timer so future wakeups still occur. */
    UTIL_TIMER_SetPeriod(&timerPingOneShot, APP_CYCLE_DELAY_MS);
    UTIL_TIMER_Start(&timerPingOneShot);
#endif
  }
  /* else: already have an outstanding PING, wait for PONG or timeout */
#endif /* APP_ROLE_INITIATOR */

  /* =========================================================================
   * STEP 3 — EVENT-DRIVEN RX REARM.
   *
   * RadioRx() is called ONLY when radio_rearm_pending is set — an explicit
   * one-shot event flag set by callbacks that require a new RX arm.
   * Timer-only wakes do NOT set this flag and never re-arm RX.
   * If TX is active (TX_PING / TX_PONG), do NOT call RadioRx().
   * ========================================================================= */
  if (radio_rearm_pending)
  {
    radio_rearm_pending = 0;
    /* Do not re-arm RX while TX is active — OnTxDone will re-arm. */
    if (AppState != APP_STATE_TX_PING && AppState != APP_STATE_TX_PONG)
    {
      RadioRx();
    }
  }
  /* AppState still in WAIT_PONG with no pending flag: receiver idle.
   * This can happen if a callback set state but did not set the flag
   * (should not occur with correct callback implementations). */

  /* Do NOT self-schedule — we are driven by radio callbacks and timer events only */
}

static void OnledEvent(void *context)
{
  HAL_GPIO_TogglePin(LED2_GPIO_Port, LED2_Pin); /* LED_GREEN */
  HAL_GPIO_TogglePin(LED3_GPIO_Port, LED3_Pin); /* LED_RED */
  UTIL_TIMER_Start(&timerLed);
}

/* USER CODE END PrFD */
