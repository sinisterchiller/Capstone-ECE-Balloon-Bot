/*
 * system_modes.h
 *
 *  Created on: Nov 1, 2026
 *      Author: zulu4
 */

#ifndef INC_SYSTEM_MODES_H_
#define INC_SYSTEM_MODES_H_

#include "main.h"
#include <stdbool.h>


typedef enum
{
    MODE_NORMAL,
    MODE_EVENT,
    MODE_LOW_POWER

} OperatingMode;

/* System mode management functions */
void SystemModes_Init(void);

void SystemModes_Update(
    bool eventDetected,
    bool lowBattery
);

OperatingMode SystemModes_GetCurrent(void);

#endif /* INC_SYSTEM_MODES_H_ */
