/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    telemetry_gps_status.h
  * @brief   Reads the GPS status snapshot published by CPU1 for telemetry.
  *
  * The snapshot itself lives in the shared SRAM1 region described by
  * gnss_vcp_mailbox.h; CPU1 updates it from every decoded GGA. This module is
  * the consumer side used by the CPU2 telemetry application: it takes a
  * coherent copy of the snapshot and decides, at the moment of consumption,
  * whether the fix may still be presented as current.
  *
  * The two cores do not share a clock. CPU1 stamps a fix with its own
  * HAL_GetTick(), i.e. SysTick milliseconds counted from CPU1 reset, while
  * CPU2 counts RTC ticks at a different rate from the free-running RTC
  * counter (CM0PLUS/Core/Src/sys_app.c returns TIMER_IF_GetTimerValue()).
  * Subtracting a CPU1 stamp from a CPU2 counter is therefore meaningless: with
  * the CPU2 origin above the CPU1 origin every fix looks stale, and with the
  * CPU2 origin below it every stale fix looks current.
  *
  * The age of a fix is consequently measured on the consumer's clock alone.
  * The consumer remembers which publication it last saw (the seqlock `seq` of
  * the snapshot) together with its own counter value when that publication
  * first appeared, and the age is the elapsed value of that same counter.
  * Counter and rate are passed by the caller, so the comparison with
  * TELEMETRY_MAX_FIX_AGE_MS happens in one time domain and the modular
  * difference stays exact at the counter's own wrap. A new publication
  * restarts the measurement; a frozen one - CPU1 stopped, faulted, held in
  * reset or no longer publishing - keeps ageing and expires on its own, no
  * matter what the CPU1 timestamps in the snapshot still say.
  *
  * That measurement cannot date the first publication a consumer sees. CPU1
  * may have published it long before CPU2 booted, or left it behind when it
  * halted, so the elapsed counter value of the first read bounds nothing: an
  * old fix would look age zero. The first coherent publication is therefore
  * only remembered and never presented. A fix becomes presentable once a read
  * observes that publication replaced within one age window, because that
  * replacement was then published after a read of this core. CPU2 sends no fix
  * in the first telemetry interval after a (re)start and a fix from a later
  * interval on, as soon as CPU1 publishes again; a snapshot inherited from an
  * earlier run can never travel as current, however old it is.
  *
  * A read that finds no initialised snapshot, a snapshot that stays busy for
  * every attempt, a no-fix status or an expired fix all resolve to the same
  * answer: a coherent no-fix status, so the caller always has something it may
  * encode and transmit. Old coordinates are never handed out as a current fix.
  ******************************************************************************
  */
/* USER CODE END Header */
/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __TELEMETRY_GPS_STATUS_H__
#define __TELEMETRY_GPS_STATUS_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include <stdbool.h>
#include <stdint.h>

#include "gnss_vcp_mailbox.h"
#include "telemetry.h"

/* Exported types ------------------------------------------------------------*/
/**
  * @brief Consumer-side record of the last GPS status publication observed.
  *
  * The fix age is the time elapsed on the reading core's counter since the
  * publication that carried the fix was first observed, so the tracker holds
  * that publication and the local counter value of its first observation.
  * `change_seen` records whether that publication was observed to replace an
  * earlier one within one age window, i.e. whether the counter value it holds
  * actually bounds its age: only then may it be presented, and only until the
  * window has elapsed. It must be zero-initialised before the first read (all
  * zero means "nothing observed yet", which is also the state after a CPU2
  * reset) and then keep its value between reads of the same core.
  *
  * @note Do not copy the tracker between cores or reset it while the
  *       application runs: the recorded counter belongs to one clock.
  */
typedef struct
{
  uint32_t sequence;        /*!< seqlock sequence of the observed publication */
  uint32_t observed_ticks;  /*!< reading core's counter when it was first observed */
  bool     observed;        /*!< false until a publication has been recorded */
  bool     change_seen;     /*!< publication was seen to replace an earlier one
                                 within one age window, so observed_ticks bounds
                                 its age */
} TelemetryGpsSnapshotWatch;

/* Exported functions --------------------------------------------------------*/
/**
  * @brief  Takes a coherent, freshness-checked copy of the shared GPS status.
  * @param  shared  Shared snapshot published by CPU1, i.e. GNSS_VCP_GPS_STATUS.
  * @param  now_ticks  Current value of the reading core's free-running counter
  *                 (it may wrap). Never a value produced by the other core: it
  *                 is only ever compared with another value of this counter.
  * @param  ticks_per_second  Rate of that counter, i.e. how many counts the
  *                 reading core's clock produces per second. Zero or an
  *                 unknown rate is reported as no fix.
  * @param  watch   Zero-initialised tracker, reused for every call.
  * @param  status  Receives the status to use for telemetry; filled no-fix
  *                 whenever no fix may be presented.
  * @return true when a current valid fix was read; false when the caller must
  *         treat the balloon as having no fix, with `status` holding a no-fix
  *         status (fix_valid 0, coordinates and satellite count 0). False
  *         covers: a publication this core has not yet seen replaced (the
  *         first one it sees, and any one observed too late), an
  *         uninitialised shared status (CPU1 image without GNSS_VCP_Init(),
  *         random RAM), a snapshot that could not be copied because CPU1 kept
  *         writing, a no-fix status published by CPU1, and a fix whose observed
  *         publication is older than TELEMETRY_MAX_FIX_AGE_MS on the reading
  *         core's clock.
  * @note   A fix is presented only after this core has seen the publication
  *         replace an earlier one within one age window, so a snapshot CPU1
  *         published before this core started is never presented. The caller
  *         reads periodically (the telemetry interval), so a running CPU1
  *         supplies such a replacement within the first interval. The price of
  *         the guarantee is one interval reported as no fix after a (re)start,
  *         and one after a gap between two reads longer than the maximum age.
  * @note   No argument may be NULL: a NULL `status` is not written, any other
  *         NULL argument is reported as no fix with `status` filled in.
  * @note   Call only from the consumer core (CPU2). The seqlock copy is bounded
  *         (a few attempts, no waiting), so this is safe in a sequencer task.
  */
bool Telemetry_ReadGpsStatusSnapshot(const GnssVcpGpsStatus *shared,
                                     uint32_t now_ticks,
                                     uint32_t ticks_per_second,
                                     TelemetryGpsSnapshotWatch *watch,
                                     TelemetryGpsStatus *status);

#ifdef __cplusplus
}
#endif

#endif /* __TELEMETRY_GPS_STATUS_H__ */
