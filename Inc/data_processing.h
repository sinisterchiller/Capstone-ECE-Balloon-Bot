/*
 * data_processing.h
 *
 *  Created on: Nov 1, 2026
 *      Author: zulu4
 */

#ifndef INC_DATA_PROCESSING_H_
#define INC_DATA_PROCESSING_H_

#include "main.h"
#include "sensors.h"
#include "gps.h"
#include <stdbool.h>

/* Processed flight information */
typedef struct
{
    float verticalSpeed;
    float altitudeChange;

    bool atmosphericEventDetected;
    bool rapidAltitudeChange;

} ProcessedData;

/* Data processing functions */
void DataProcessing_Init(void);

void DataProcessing_Process(
    SensorData *sensorData,
    GPS_Data *gpsData,
    ProcessedData *processedData
);

void DataProcessing_DetectEvents(
    ProcessedData *processedData
);

#endif /* INC_DATA_PROCESSING_H_ */
