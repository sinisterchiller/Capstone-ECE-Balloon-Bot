/*
 * system_modes.c
 *
 *  Created on: Nov 1, 2026
 *      Author: zulu4
 */


// identifying operation modes


#include "system_modes.h"
#include <stdbool.h>

/*
 * SYSTEM MODE MANAGEMENT
 *
 * Responsibilities:
 *
 * - Manage normal operating mode.
 * - Manage event operating mode.
 * - Manage low-power operating mode.
 * - Determine operating priorities.
 * - Adjust system scheduling intervals.
 */


/* Current operating mode */

static OperatingMode currentMode = MODE_NORMAL;


/* Initialize system modes */

void SystemModes_Init(void)
{
    currentMode = MODE_NORMAL;
}


/* Update operating mode */

void SystemModes_Update(
    bool eventDetected,
    bool lowBattery
)
{
    // TODO: Determine appropriate operating mode.
}


/* Return current operating mode */

OperatingMode SystemModes_GetCurrent(void)
{
    return currentMode;
}
