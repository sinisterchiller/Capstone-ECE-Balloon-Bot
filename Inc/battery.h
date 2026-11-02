/*
 * battery.h
 *
 *  Created on: Nov 1, 2026
 *      Author: zulu4
 */

#ifndef INC_BATTERY_H_
#define INC_BATTERY_H_

#include "main.h"
#include <stdbool.h>

/* Battery measurement information */
typedef struct
{
    float batteryVoltage;
    float batteryPercentage;

    bool lowBattery;
    bool batteryAvailable;

} BatteryData;

/* Battery module functions */
void Battery_Init(void);
void Battery_ReadData(BatteryData *data);
void Battery_CheckLevel(BatteryData *data);



#endif /* INC_BATTERY_H_ */
