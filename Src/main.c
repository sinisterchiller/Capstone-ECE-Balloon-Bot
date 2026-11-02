
#include "main.h"
#include <stdbool.h>
#include <stdio.h>

/* USER CODE BEGIN PV */

#include "sensors.h"
#include "gps.h"
#include "battery.h"
#include "data_processing.h"
#include "system_modes.h"
#include "storage.h"
#include "communication.h"


SensorData sensorData = {0};
GPS_Data gpsData = {0};
BatteryData batteryData = {0};
ProcessedData processedData = {0};

// System scheduling variables (not a timer)

uint32_t lastMeasurementTime = 0;
uint32_t lastStorageTime = 0;
uint32_t lastTransmissionTime = 0;

// Scheduling intervals (milliseconds)

uint32_t measurementInterval = 5000;
uint32_t storageInterval = 10000;
uint32_t transmissionInterval = 30000;


/* USER CODE BEGIN 0 */

// Future application-level functions

/* USER CODE END 0 */
void SystemClock_Config(void);

int main(void)
{
    // Initialize STM32 hardware

    HAL_Init();

    SystemClock_Config();

    // Initialize gpio,etc here


    /* USER CODE BEGIN 2 */

    // Initialize application modules

    Sensors_Init();

    GPS_Init();

    Battery_Init();

    DataProcessing_Init();

    SystemModes_Init();

    Storage_Init();

    Communication_Init();


    // Initialize scheduling times

    uint32_t currentTime = HAL_GetTick();

    lastMeasurementTime = currentTime;
    lastStorageTime = currentTime;
    lastTransmissionTime = currentTime;

    /* USER CODE END 2 */


    // Main application loop

    while (1)
    {
        currentTime = HAL_GetTick();

        /*
          1. Read sensor measurements

          2. Read GPS information

          3. Monitor battery status

          4. Process collected measurements

          5. Update operating mode

          6. Save data when required

          7. Transmit telemetry when required

          8. Check system health
         */

        /* TODO: Implement application scheduling */

    }

}


/* System clock configuration */
void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    if (HAL_PWREx_ControlVoltageScaling(
            PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK)
    {
        Error_Handler();
    }

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_MSI;
    RCC_OscInitStruct.MSIState = RCC_MSI_ON;
    RCC_OscInitStruct.MSICalibrationValue = 0;
    RCC_OscInitStruct.MSIClockRange = RCC_MSIRANGE_6;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;

    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
    {
        Error_Handler();
    }

    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK
                                | RCC_CLOCKTYPE_SYSCLK
                                | RCC_CLOCKTYPE_PCLK1
                                | RCC_CLOCKTYPE_PCLK2;

    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_MSI;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

    if (HAL_RCC_ClockConfig(
            &RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
    {
        Error_Handler();
    }
}


/* Error handler */
void Error_Handler(void)
{
    __disable_irq();

    while (1)
    {
    }
}


#ifdef USE_FULL_ASSERT

void assert_failed(uint8_t *file, uint32_t line)
{
    /* Add assertion reporting if required */
}

#endif


