/* SPDX-License-Identifier: MIT */
/* Boot and crash accounting: why the chip last reset, how many boots, how many
 * watchdog resets and bridge hangs. The SDK already runs a 4 s hardware watchdog
 * (cpu/wl82/setup.c) that the system task feeds; this adds a task-level monitor:
 * the UI task restarts the chip if the bridge loop stops advancing for 5 s. */
#pragma once
#include "typedef.h"

#define FM1_HEALTH_ID 15            /* syscfg record */

typedef struct {
    u32 magic;
    u32 boots;
    u32 wdt_resets;
    u32 soft_resets;
    u32 hangs;                      /* bridge loop stalls that forced a restart */
    u32 last_reason;                /* SDK sys_reset_type bits */
    u16 pad, crc;
} fm1_health;

void health_init(void);                     /* at boot: classify the reset, bump the counters, store */
const fm1_health *health_get(void);
const char *health_reason_text(u32 reason);
void health_bridge_alive(void);             /* bridge loop heartbeat */
void health_check(void);                    /* from the UI tick, every ~1 s */
void health_reset_counters(void);
