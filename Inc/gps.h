/*
 * gps.h
 *
 *  Created on: Nov 1, 2026
 *      Author: zulu4
 */

#ifndef INC_GPS_H_
#define INC_GPS_H_

#include "main.h"
#include <stdbool.h>

/* GPS measurement information */
typedef struct
{
    float latitude;
    float longitude;
    float altitude;
    float groundSpeed;

    bool gpsAvailable;

} GPS_Data;

/* GPS module functions */
void GPS_Init(void);
void GPS_ReadData(GPS_Data *data);
GPS_Data GPS_GetData(void);

#endif /* INC_GPS_H_ */
