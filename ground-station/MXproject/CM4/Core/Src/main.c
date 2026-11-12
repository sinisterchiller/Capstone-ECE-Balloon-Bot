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
#include "gnss_vcp_mailbox.h"
#if (FW_ROLE == PAYLOAD)
#include "i2c.h"
#include "mpu.h"
#endif
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

#if (FW_ROLE == PAYLOAD)
/* MPU bring-up retry policy. A probe or init that fails at boot is retried
   from the main loop: MPU_BRINGUP_RETRY_MS spaces the attempts on the SysTick
   time base (no waiting loop) and MPU_BRINGUP_MAX_ATTEMPTS bounds them, so a
   board without the sensor stops touching the I2C bus instead of probing it for
   the whole flight. */
#define MPU_BRINGUP_MAX_ATTEMPTS 5u
#define MPU_BRINGUP_RETRY_MS     1000u
#endif
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
#if (FW_ROLE == PAYLOAD)
MPU_Handle_t hmpu = {0};

/* Bring-up bookkeeping of an MPU that did not answer, see
   MPU_BRINGUP_MAX_ATTEMPTS. Attempts made so far, and the SysTick value at
   which the next one may run. */
static uint8_t  mpu_bringup_attempts;
static uint32_t mpu_next_bringup_tick;

/* Poll outcome last reported on the VCP trace. A sensor that answers the probe
   but never delivers a reading is reported by its first failing poll, so the
   boot value MPU_TRACE_UNREPORTED is distinct from a reported failure:
   without it the first read error stayed silent and a failing poll looked like
   an intercore update problem on the dashboard. A sensor that never came up is
   reported by MPU_BringUpAttempt() instead. Tracing the transitions only
   keeps a lost or regained sensor at one line each instead of one line per
   poll. */
#define MPU_TRACE_UNREPORTED 0u
#define MPU_TRACE_VALID      1u
#define MPU_TRACE_FAILED     2u
static uint8_t  mpu_trace_state;

/* MPU VCP logging: 100 Hz poll rate, log at 1 Hz (every 100 polls). */
#define MPU_LOG_POLL_INTERVAL  100u
static uint16_t mpu_log_poll_counter;

/* Pre-probe I2C diagnostic retry bookkeeping. The diagnostic is captured
   before CPU2 is released (so CPU2 may not yet be draining the ring).
   If the initial enqueue fails, we retry from the main loop after CPU2
   release with a bounded schedule. */
#define I2C_DIAG_MAX_RETRIES    10u
#define I2C_DIAG_RETRY_MS       100u
static uint8_t  i2c_diag_captured       = 0u;
static uint8_t  i2c_diag_posted         = 0u;
static uint8_t  i2c_diag_retries        = 0u;
static uint32_t i2c_diag_next_retry_tick = 0u;
static uint32_t i2c_diag_clk            = 0u;
static uint32_t i2c_diag_sda            = 0u;
static uint32_t i2c_diag_scl            = 0u;
#endif
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
#if (FW_ROLE == PAYLOAD)
static void MPU_BringUpAttempt_Local(void);
#endif
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
#if (FW_ROLE == PAYLOAD)
/**
  * @brief  One MPU bring-up attempt: probe the device, then run the driver
  *         init sequence (accel/gyro + magnetometer if MPU-9250).
  * @note   Called from the boot code and from the main loop only. An attempt is
  *         one bounded I2C transaction plus the datasheet mode-switch delays,
  *         never a waiting loop, so the caller keeps running its other work
  *         between attempts.
  * @note   The power-on delay is not part of an attempt: main() waits it once
  *         before the first probe, and the sensor has been supplied ever since.
  */
static void MPU_BringUpAttempt_Local(void)
{
  if (mpu_bringup_attempts < MPU_BRINGUP_MAX_ATTEMPTS)
  {
    mpu_bringup_attempts++;
  }

  HAL_StatusTypeDef status = MPU_BringUpAttempt(&hmpu);

  if (status != HAL_OK)
  {
    /* Failed attempt: keep the handle uninitialised so the main loop publishes
       invalid data and comes back here until the budget is spent. */
    hmpu.initialized = 0u;
    mpu_next_bringup_tick = HAL_GetTick() + MPU_BRINGUP_RETRY_MS;

    if (mpu_bringup_attempts >= MPU_BRINGUP_MAX_ATTEMPTS)
    {
      (void)GNSS_VCP_PostText("MPU fail: bring-up retries exhausted");
    }
  }
  else
  {
    /* Success: MPU_BringUpAttempt logged the model detection */
    char buf[64];
    int len = snprintf(buf, sizeof(buf),
                       "MPU ok: %s initialized",
                       MPU_ModelToString(hmpu.model));
    if (len > 0 && len < (int)sizeof(buf))
    {
      (void)GNSS_VCP_PostText(buf);
    }
  }
}

/**
  * @brief  Maps the MPU driver's model enumeration to the telemetry wire model
  *         identifier stored in the shared snapshot.
  * @note   The two numberings differ: the driver enumerates MPU-9250 = 1 and
  *         MPU-6500 = 2, while the wire/snapshot identifiers are 1 = MPU-6500
  *         and 2 = MPU-9250 (GNSS_VCP_MPU_MODEL_*). The mapping is explicit so
  *         a ground station never confuses the two sensors.
  */
static uint8_t MPU_ModelToWireId(MPU_Model_t model)
{
  switch (model)
  {
    case MPU_MODEL_9250:
      return GNSS_VCP_MPU_MODEL_9250;
    case MPU_MODEL_6500:
      return GNSS_VCP_MPU_MODEL_6500;
    default:
      return GNSS_VCP_MPU_MODEL_UNKNOWN;
  }
}
#endif /* FW_ROLE == PAYLOAD */
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

  /* MPU on I2C2 (PA11/PA12, AF4, 100 kHz, address 0x68 or 0x69) */
  MX_I2C2_Init();

  /* One-time I2C2/PA11-PA12 register snapshot before first MPU probe. */
  {
    uint32_t gpioa_moder = (GPIOA->MODER >> 22) & 0xFu;           /* PA11/PA12 mode nibbles */
    uint32_t gpioa_afr   = (GPIOA->AFR[1] >> 12) & 0xFFu;         /* PA11/PA12 AF nibbles */
    uint32_t gpioa_otyper = (GPIOA->OTYPER >> 11) & 0x3u;         /* PA11/PA12 open-drain bits */
    uint32_t i2c2_cr1    = I2C2->CR1;                             /* full CR1 register */
    uint32_t i2c2_timingr = I2C2->TIMINGR;                        /* full timing register */

    /* Expected values (peripheral config only, not electrical waveforms):
     *   MODER[22:25]  = 0xA  (1010 = AF for both PA11, PA12)
     *   AFR[1][12:19] = 0x44 (AF4 for both PA11, PA12)
     *   OTYPER[11:12] = 0x3  (open-drain for both)
     *   CR1[0]        = 1    (I2C enabled, nominal target)
     *   TIMINGR       = 0x00201313 (nominal 100 kHz target @ 4 MHz PCLK, not independently verified) */
    char buf[128];
    int len = snprintf(buf, sizeof(buf),
                       "I2C GPIO: MODE=0x%lX AF=0x%lX OD=0x%lX",
                       (unsigned long)gpioa_moder,
                       (unsigned long)gpioa_afr,
                       (unsigned long)gpioa_otyper);
    if (len > 0 && len < (int)sizeof(buf))
    {
      (void)GNSS_VCP_PostText(buf);
    }

    len = snprintf(buf, sizeof(buf),
                   "I2C HW: CR1=0x%lX TIMING=0x%lX",
                   (unsigned long)i2c2_cr1,
                   (unsigned long)i2c2_timingr);
    if (len > 0 && len < (int)sizeof(buf))
    {
      (void)GNSS_VCP_PostText(buf);
    }
  }

  /* The sensor ignores I2C for MPU_DELAY_POR_MS after its supply comes up,
     so the first probe waits for it. This is the only power-on wait of the
     boot: MPU_Init() no longer sleeps the same delay, and a probe that
     fails anyway is retried from the main loop below instead of disabling the
     sensor for the whole flight. */
  HAL_Delay(MPU_DELAY_POR_MS);

  /* Pre-probe I2C electrical/clock diagnostic: actual I2C2 kernel clock
     and live SDA/SCL line levels before first WHO_AM_I read.
     Capture values now; enqueue immediately and retry from main loop if ring full. */
  {
    i2c_diag_clk = HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_I2C2);
    i2c_diag_sda = (GPIOA->IDR >> 11) & 1u;
    i2c_diag_scl = (GPIOA->IDR >> 12) & 1u;
    i2c_diag_captured = 1u;

    char buf[64];
    int len = snprintf(buf, sizeof(buf),
                       "I2C: CLK=%lu SDA=%lu SCL=%lu",
                       (unsigned long)i2c_diag_clk,
                       (unsigned long)i2c_diag_sda,
                       (unsigned long)i2c_diag_scl);
    if (len > 0 && len < (int)sizeof(buf))
    {
      if (GNSS_VCP_PostText(buf) != 0u)
      {
        i2c_diag_posted = 1u;
      }
      else
      {
        /* Ring full (CPU2 not yet draining); schedule retry from main loop. */
        i2c_diag_next_retry_tick = HAL_GetTick() + I2C_DIAG_RETRY_MS;
      }
    }
  }

  MPU_BringUpAttempt_Local();

  /* Initialize the MPU orientation snapshot in shared memory (invalid / no
     data). Done after the MPU probe so CPU2 knows the detected model even
     before the first reading arrives. */
  GnssVcpMpuStatus *mpu_status = GNSS_VCP_MPU_STATUS;
  mpu_status->seq = 0u;
  mpu_status->update_timestamp_ms = 0u;
  mpu_status->valid = 0u;
  mpu_status->heading_valid = 0u;
  mpu_status->model = MPU_ModelToWireId(hmpu.model);
  mpu_status->heading = 0;
  mpu_status->roll = 0;
  mpu_status->pitch = 0;
  __DMB();
  mpu_status->initialized = GNSS_VCP_MPU_STATUS_INIT_MARK;
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
    /* Retry pre-probe I2C diagnostic enqueue if initial post failed (ring full
       before CPU2 was draining). Bounded retries after CPU2 release. */
    if (i2c_diag_captured && !i2c_diag_posted)
    {
      uint32_t now = HAL_GetTick();
      if ((i2c_diag_retries < I2C_DIAG_MAX_RETRIES) &&
          ((int32_t)(now - i2c_diag_next_retry_tick) >= 0))
      {
        char buf[64];
        int len = snprintf(buf, sizeof(buf),
                           "I2C: CLK=%lu SDA=%lu SCL=%lu",
                           (unsigned long)i2c_diag_clk,
                           (unsigned long)i2c_diag_sda,
                           (unsigned long)i2c_diag_scl);
        if (len > 0 && len < (int)sizeof(buf))
        {
          if (GNSS_VCP_PostText(buf) != 0u)
          {
            i2c_diag_posted = 1u;
          }
          else
          {
            i2c_diag_retries++;
            i2c_diag_next_retry_tick = now + I2C_DIAG_RETRY_MS;
          }
        }
        else
        {
          /* Formatting failed; give up to avoid busy loop. */
          i2c_diag_posted = 1u;
        }
      }
    }

    /* Hand the latest complete sentence, if any, to CPU2 for printing. */
    GNSS_VCP_Process();

    /* Periodically expire stale GPS fixes (check every loop iteration,
       the function itself handles the age threshold). */
    GNSS_VCP_CheckFixExpiration();

    /* MPU non-blocking periodic poll (~100 Hz, 10 ms interval). */
    if (hmpu.initialized)
    {
      HAL_StatusTypeDef poll_status = MPU_Poll(&hmpu);

      if (poll_status == HAL_BUSY)
      {
        /* Not time for the next poll yet; do nothing. */
      }
      else if (poll_status == HAL_OK && hmpu.valid)
      {
        /* Publish the fresh MPU reading to shared memory. Heading validity is
           the driver's magnetometer-derived flag; it is never derived from the
           accelerometer/gyroscope validity. An invalid heading is published as
           zero with its flag clear, so roll/pitch stay usable on an MPU-6500. */
        uint8_t heading_valid = hmpu.orientation.heading_valid ? 1u : 0u;
        int16_t heading_s16 = heading_valid ? (int16_t)(hmpu.orientation.heading_deg * MPU_ORIENTATION_SCALE) : 0;
        int16_t roll_s16    = (int16_t)(hmpu.orientation.roll_deg * MPU_ORIENTATION_SCALE);
        int16_t pitch_s16   = (int16_t)(hmpu.orientation.pitch_deg * MPU_ORIENTATION_SCALE);

        GNSS_VCP_PublishMpuStatus(GNSS_VCP_MPU_STATUS,
                                  hmpu.last_update_tick,
                                  1u,
                                  heading_valid,
                                  MPU_ModelToWireId(hmpu.model),
                                  heading_s16,
                                  roll_s16,
                                  pitch_s16);
        if (mpu_trace_state != MPU_TRACE_VALID)
        {
          mpu_trace_state = MPU_TRACE_VALID;
          (void)GNSS_VCP_PostText("MPU ok: readings valid");
        }

        /* Rate-limited VCP logging: 1 Hz (every 100 polls at 100 Hz).
           Uses integer fixed-point (no %f) to work with printf float disabled.
           Two short lines, each < 104 bytes (GNSS_VCP_LINE_CAPACITY). */
        mpu_log_poll_counter++;
        if (mpu_log_poll_counter >= MPU_LOG_POLL_INTERVAL)
        {
          mpu_log_poll_counter = 0u;

          /* Line 1: accel (mg), gyro (mdps), roll/pitch (centi-deg). */
          int32_t ax_mg = (int32_t)(hmpu.scaled.accel_x_g * 1000.0f);
          int32_t ay_mg = (int32_t)(hmpu.scaled.accel_y_g * 1000.0f);
          int32_t az_mg = (int32_t)(hmpu.scaled.accel_z_g * 1000.0f);
          int32_t gx_mdps = (int32_t)(hmpu.scaled.gyro_x_dps * 1000.0f);
          int32_t gy_mdps = (int32_t)(hmpu.scaled.gyro_y_dps * 1000.0f);
          int32_t gz_mdps = (int32_t)(hmpu.scaled.gyro_z_dps * 1000.0f);
          int32_t roll_cdeg = (int32_t)(hmpu.orientation.roll_deg * 100.0f);
          int32_t pitch_cdeg = (int32_t)(hmpu.orientation.pitch_deg * 100.0f);

          char buf1[104];
          int len1 = snprintf(buf1, sizeof(buf1),
                              "MPU A=%ld/%ld/%ldmg G=%ld/%ld/%ldmdps R=%ld P=%ldcdeg",
                              (long)ax_mg, (long)ay_mg, (long)az_mg,
                              (long)gx_mdps, (long)gy_mdps, (long)gz_mdps,
                              (long)roll_cdeg, (long)pitch_cdeg);
          if (len1 > 0 && len1 < (int)sizeof(buf1))
          {
            (void)GNSS_VCP_PostText(buf1);
          }

          /* Line 2: heading, model, sample sequence. */
          char buf2[104];
          int len2 = 0;
          if (hmpu.model == MPU_MODEL_9250 && hmpu.orientation.heading_valid)
          {
            int32_t heading_cdeg = (int32_t)(hmpu.orientation.heading_deg * 100.0f);
            len2 = snprintf(buf2, sizeof(buf2),
                            "MPU H=%ldcdeg M=%s S=%lu",
                            (long)heading_cdeg,
                            MPU_ModelToString(hmpu.model),
                            (unsigned long)hmpu.last_update_tick);
          }
          else
          {
            /* MPU-6500 or MPU-9250 without valid heading */
            len2 = snprintf(buf2, sizeof(buf2),
                            "MPU H=UNAVAILABLE M=%s S=%lu",
                            MPU_ModelToString(hmpu.model),
                            (unsigned long)hmpu.last_update_tick);
          }
          if (len2 > 0 && len2 < (int)sizeof(buf2))
          {
            (void)GNSS_VCP_PostText(buf2);
          }
        }
      }
      else
      {
        /* The poll did not produce a reading: either it ran but the I2C read
           failed (HAL_OK with valid cleared), or the transaction itself
           errored or timed out. Publish invalid status immediately so CPU2
           does not see stale data, and keep seq advancing so it knows we're
           still alive but the data is bad. MPU_Poll already cleared
           hmpu.valid and incremented consec_failures. The trace line is
           written on the first failure and on a valid-to-failed transition
           only, not on every failing poll. */
        GNSS_VCP_PublishMpuStatus(GNSS_VCP_MPU_STATUS,
                                  HAL_GetTick(),
                                  0u,
                                  0u,
                                  MPU_ModelToWireId(hmpu.model),
                                  0, 0, 0);
        if (mpu_trace_state != MPU_TRACE_FAILED)
        {
          mpu_trace_state = MPU_TRACE_FAILED;
          (void)GNSS_VCP_PostText("MPU fail: read failed, driver retries");
        }
      }
    }
    else
    {
      /* MPU not initialised (the boot probe or init failed, and every retry
         since): retry the bring-up while the attempt budget lasts, and publish
         invalid status once a second so CPU2 doesn't see stale data from a
         previous run. */
      static uint32_t last_invalid_publish = 0;
      uint32_t now = HAL_GetTick();

      if ((mpu_bringup_attempts < MPU_BRINGUP_MAX_ATTEMPTS) &&
          ((int32_t)(now - mpu_next_bringup_tick) >= 0))
      {
        MPU_BringUpAttempt_Local();
        now = HAL_GetTick();
      }

      if ((now - last_invalid_publish) >= 1000)
      {
        GNSS_VCP_PublishMpuStatus(GNSS_VCP_MPU_STATUS,
                                  now,
                                  0u,
                                  0u,
                                  MPU_ModelToWireId(hmpu.model),
                                  0, 0, 0);
        last_invalid_publish = now;
      }
    }
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
