/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    gnss_vcp_trace.c
  * @brief   Prints the NMEA lines received by CPU1 on the ST-LINK VCP.
  *
  * CPU2 owns USART2 (PA2/PA3, 115200 8N1) and its DMA channel for the SubGHz_Phy
  * trace, so the GPS sentences cannot be transmitted by CPU1. CPU1 posts them
  * into the shared ring of gnss_vcp_mailbox.h; here a periodic timer only
  * schedules a sequencer task, and that task drains the ring into the existing
  * advanced trace (DMA, non-blocking). The timer interrupt never prints.
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "gnss_vcp_trace.h"
#include "gnss_vcp_mailbox.h"
#include "platform.h"
#include "sys_app.h"
#include "stm32_seq.h"
#include "stm32_timer.h"
#include "utilities_conf.h"

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/**
  * @brief Drain period. The ring holds 512 bytes, about 0.5 s of NMEA at
  *        9600 baud, so this only bounds the printing latency and the number
  *        of CPU2 wake-ups.
  */
#define GNSS_VCP_POLL_PERIOD_MS 50u

/**
  * @brief Lines printed per task run, bounding the time spent away from the
  *        radio tasks when the ring has accumulated a burst.
  */
#define GNSS_VCP_MAX_LINES_PER_RUN 4u
/* USER CODE END PD */

/* Private variables ---------------------------------------------------------*/
volatile uint32_t gnss_vcp_trace_dropped;

/* USER CODE BEGIN PV */
static UTIL_TIMER_Object_t gnss_vcp_poll_timer;
static char gnss_vcp_line[GNSS_VCP_LINE_CAPACITY];
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */
static void GNSS_VCP_OnPollTimer(void *context);
static void GNSS_VCP_TraceProcess(void);
/* USER CODE END PFP */

/* Exported functions --------------------------------------------------------*/
void GNSS_VCP_TraceInit(void)
{
  UTIL_SEQ_RegTask((1u << CFG_SEQ_Task_Gnss_Vcp_Trace), UTIL_SEQ_RFU, GNSS_VCP_TraceProcess);
  UTIL_TIMER_Create(&gnss_vcp_poll_timer, GNSS_VCP_POLL_PERIOD_MS, UTIL_TIMER_PERIODIC,
                    GNSS_VCP_OnPollTimer, NULL);
  UTIL_TIMER_Start(&gnss_vcp_poll_timer);
}

/* USER CODE BEGIN EF */

/* USER CODE END EF */

/* Private functions ---------------------------------------------------------*/
/**
  * @brief  Timer callback: schedules the drain task and returns.
  */
static void GNSS_VCP_OnPollTimer(void *context)
{
  (void)context;
  UTIL_SEQ_SetTask((1u << CFG_SEQ_Task_Gnss_Vcp_Trace), CFG_SEQ_Prio_0);
}

/**
  * @brief  Sequencer task: prints the complete lines waiting in the ring.
  * @note   One trace call per line keeps every line CR LF framed on the VCP;
  *         the ring only ever holds whole lines (see gnss_vcp_mailbox.h).
  */
static void GNSS_VCP_TraceProcess(void)
{
  GnssVcpMailbox *mailbox = GNSS_VCP_MAILBOX;
  uint32_t printed = 0u;

  while (printed < GNSS_VCP_MAX_LINES_PER_RUN)
  {
    uint32_t read = mailbox->read;
    uint32_t pending;
    uint16_t length = 0u;

    __DMB(); /* sample the producer index before the bytes it published */
    pending = mailbox->write - read;

    if (pending == 0u)
    {
      return; /* no complete line: print nothing */
    }

    if (pending >= GNSS_VCP_RING_SIZE)
    {
      /* Not a valid producer/consumer pair, i.e. the mailbox was never
         initialised by CPU1 (CPU1 image without GNSS_VCP_Init()). Resynchronise
         instead of printing random RAM. */
      mailbox->read = mailbox->write;
      return;
    }

    while ((length < pending) && (length < (GNSS_VCP_LINE_CAPACITY - 1u)))
    {
      gnss_vcp_line[length] = (char)mailbox->ring[(read + length) & (GNSS_VCP_RING_SIZE - 1u)];
      length++;
      if (gnss_vcp_line[length - 1u] == '\n')
      {
        break;
      }
    }

    mailbox->read = read + length; /* consume before printing: never re-prints */

    if (UTIL_ADV_TRACE_COND_Send(VLEVEL_M, T_REG_OFF, TS_OFF, (const uint8_t *)gnss_vcp_line,
                                 length) != UTIL_ADV_TRACE_OK)
    {
      gnss_vcp_trace_dropped++;
    }
    printed++;
  }
}
