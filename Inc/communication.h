/*
 * communication.h
 *
 *  Created on: Nov 1, 2026
 *      Author: zulu4
 */

#ifndef INC_COMMUNICATION_H_
#define INC_COMMUNICATION_H_

#include "main.h"
#include "sensors.h"
#include "gps.h"
#include "battery.h"

/* Communication module functions */
void Communication_Init(void);

void Communication_PackageData(
    SensorData *sensorData,
    GPS_Data *gpsData,
    BatteryData *batteryData
);

void Communication_Transmit(void);

void Communication_CheckStatus(void);

#endif /* INC_COMMUNICATION_H_ */
