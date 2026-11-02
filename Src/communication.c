/*
 * communication.c
 *
 *  Created on: Nov 1, 2026
 *      Author: zulu4
 */

//Sending info via Lora


#include "communication.h"

/*
 * COMMUNICATION MODULE
 *
 * Responsibilities:
 *
 * - Initialize telemetry hardware.
 * - Prepare telemetry data.
 * - Package sensor, GPS and battery information.
 * - Transmit telemetry to the ground station.
 * - Monitor communication status.
 */


/* Initialize communication hardware */

void Communication_Init(void)
{
    /* TODO: Initialize radio hardware */
}


/* Prepare telemetry information */

void Communication_PackageData(
    SensorData *sensorData,
    GPS_Data *gpsData,
    BatteryData *batteryData
)
{
    /*
     * TODO: Combine measurement information.
     * TODO: Prepare telemetry packet.
     */
}


/* Transmit telemetry */

void Communication_Transmit(void)
{
    /* TODO: Transmit prepared telemetry packet */
}


/* Check communication status */

void Communication_CheckStatus(void)
{
    /* TODO: Monitor communication availability */
}

