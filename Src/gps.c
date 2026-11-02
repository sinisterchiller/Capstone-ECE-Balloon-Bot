/*
 * gps.c
 *
 *  Created on: Nov 1, 2026
 *      Author: zulu4
 */


//gps location


#include "gps.h"

/*
  GPS MODULE

  Responsibilities:

  - Initialize GPS hardware.
  - Receive GPS information.
  - Extract latitude and longitude.
  - Obtain altitude.
  - Obtain ground speed.
  - Monitor GPS availability.
 */


// Initialize GPS

void GPS_Init(void)
{
    // TODO: Initialize GPS hardware
}


// Read GPS measurements

void GPS_ReadData(GPS_Data *data)
{
    /*
      TODO: Read GPS information.
      TODO: Update latitude and longitude.
      TODO: Update altitude and ground speed.
     */
}


// Return latest GPS information

GPS_Data GPS_GetData(void)
{
    GPS_Data data = {0};

    // TODO: Return latest GPS measurements

    return data;
}
