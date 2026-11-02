/*
 * sensors.c
 *
 *  Created on: Nov 1, 2026
 *      Author: zulu4
 */

//Temperature, humidity, pressure data

#include "sensors.h"
#include <stdbool.h>
#include <stdio.h>


// Initialize sensors

void Sensors_Init(void)
{
    //sensors initialised
}


/* Read all available sensors */

void Sensors_ReadAll(SensorData *data)
{
    /*
      TODO:  temperature humidity pressure acceleration
     */
}


// Return sensor availability in case sensors die or malfunction

SensorStatus Sensors_GetStatus(void)
{
    SensorStatus status = {0};

    //return actual sensor availability

    return status;
}
