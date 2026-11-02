/*
 * storage.c
 *
 *  Created on: Nov 1, 2026
 *      Author: zulu4
 */


//SD card shifting


#include "storage.h"

/*
 * STORAGE MODULE
 *
 * Responsibilities:
 *
 * - Initialize onboard storage.
 * - Store sensor measurements.
 * - Store GPS information.
 * - Store battery information.
 * - Retrieve recorded measurements.
 *
 * Actual storage hardware and filesystem
 * will be implemented later.
 */


// Initialize

void Storage_Init(void)
{
    //Initialize
}


// Save collected measurements

void Storage_SaveData(
    SensorData *sensorData,
    GPS_Data *gpsData,
    BatteryData *batteryData
)
{
    /*
      TODO: Prepare measurement record.
      TODO: Save measurement record.
      TODO: Handle storage errors.
     */
}


// Retrieve stored measurements
// may not be needed(remove later then)

void Storage_ReadData(void)
{
    /* TODO: Retrieve previously stored records */
}
