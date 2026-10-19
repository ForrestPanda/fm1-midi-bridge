/* SPDX-License-Identifier: MIT */
/* Boot and crash accounting: why the chip last reset, how many boots, how many
 * watchdog resets and bridge hangs. The SDK already runs a 4 s hardware watchdog
 * (cpu/wl82/setup.c) that the system task feeds; this adds a task-level monitor:
 * the UI task restarts the chip if the bridge loop stops advancing for 5 s. */
#pragma once
#include "typedef.h"

#define FM1_HEALTH_ID 15            /* syscfg record */
#define FM1_STALLLOG_ID 20          /* syscfg record: the log's last bytes, saved before a stall reset */
#define FM1_STALLLOG_LEN 400

typedef struct {
    u32 magic;
    u32 boots;
    u32 wdt_resets;
    u32 soft_resets;
    u32 hangs;                      /* bridge loop stalls that forced a restart */
    u32 last_reason;                /* SDK sys_reset_type bits */
    u32 unclean;                    /* boots in a row that never reached a clean run (health_mark_clean) */
    u32 stage;                      /* the last step the panel init announced before the previous boot ended (lcd.c) */
    u16 pad, crc;
} fm1_health;

#define HEALTH_SAFE_BOOTS 3         /* this many unclean boots in a row: the UI skips the panel (ui.c) */

void health_init(void);                     /* at boot: classify the reset, bump the counters, store */
const fm1_health *health_get(void);
const char *health_reason_text(u32 reason);
void health_bridge_alive(void);             /* bridge loop heartbeat */
void health_check(void);                    /* from the UI tick, every ~1 s */
void health_reset_counters(void);
void health_mark_clean(void);
void health_stage(u32 stage);                /* stored at once: a crash right after leaves it for the next boot's log */               /* the firmware ran long enough: this boot was not a crash loop */
