/*
 * storage.h
 *
 *  Created on: Nov 1, 2026
 *      Author: zulu4
 */

#ifndef INC_STORAGE_H_
#define INC_STORAGE_H_

#include "main.h"
#include "sensors.h"
#include "gps.h"
#include "battery.h"

/* Storage module functions */
void Storage_Init(void);

void Storage_SaveData(
    SensorData *sensorData,
    GPS_Data *gpsData,
    BatteryData *batteryData
);

void Storage_ReadData(void);

#endif /* INC_STORAGE_H_ */
