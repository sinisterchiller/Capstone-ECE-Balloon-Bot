/*
 * sensors.h
 *
 *  Created on: Nov 1, 2026
 *      Author: zulu4
 */

#ifndef INC_SENSORS_H_
#define INC_SENSORS_H_


#include "main.h"
#include <stdbool.h>

/* Environmental and motion sensor measurements */
typedef struct
{
    float temperature;
    float humidity;
    float pressure;
    float acceleration;
    float verticalSpeed;

} SensorData;

/* Availability of individual sensors */
typedef struct
{
    bool sht45Available;
    bool ms5611Available;
    bool imuAvailable;

} SensorStatus;

/* Sensor module functions */
void Sensors_Init(void);
void Sensors_ReadAll(SensorData *data);
SensorStatus Sensors_GetStatus(void);


#endif /* INC_SENSORS_H_ */
