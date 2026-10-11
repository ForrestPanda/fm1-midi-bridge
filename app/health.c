/* SPDX-License-Identifier: MIT */
#include "system/includes.h"
#include "app_config.h"
#include "asm/cpu.h"
#include "asm/system_reset_reason.h"
#include "syscfg_id.h"
#include "health.h"
#include "util/crc16.h"

#define LOG_TAG_CONST       APP
#define LOG_TAG             "[HEALTH]"
#define LOG_ERROR_ENABLE
#define LOG_INFO_ENABLE
#include "debug.h"

#define HEALTH_MAGIC 0x48544C48u    /* "HLTH" */

static fm1_health h;
static volatile u32 bridge_beats;
static u32 seen_beats, stale_ms;

static void store(void)
{
    h.magic = HEALTH_MAGIC;
    h.crc = (u16)crc16_1021((const u8 *)&h, sizeof h - 2, 0);
    syscfg_write(FM1_HEALTH_ID, &h, sizeof h);
}

void health_init(void)
{
    fm1_health r;
    int reason = system_reset_reason_get();
    if (syscfg_read(FM1_HEALTH_ID, &r, sizeof r) == (int)sizeof r && r.magic == HEALTH_MAGIC &&
        crc16_1021((const u8 *)&r, sizeof r - 2, 0) == r.crc) h = r;
    else memset(&h, 0, sizeof h);
    h.boots++;
    h.last_reason = (u32)reason;
    if (reason & SYS_RST_WDT) h.wdt_resets++;
    if (reason & SYS_RST_SOFT) h.soft_resets++;
    store();
    log_info("boot %u, reset reason %s (%x)", (unsigned)h.boots, health_reason_text(h.last_reason), (unsigned)reason);
}

const fm1_health *health_get(void) { return &h; }

const char *health_reason_text(u32 r)
{
    if (r & SYS_RST_WDT) return "watchdog";
    if (r & SYS_RST_SOFT) return "software";
    if (r & SYS_RST_LONG_PRESS) return "long press";
    if (r & SYS_RST_VDDIO_LOW_PWR) return "low power";
    if (r & SYS_RST_VDDIO_PWR_ON) return "power on";
    if (r & SYS_RST_PORT_WKUP) return "port wake";
    if (r & SYS_RST_ALM_WKUP) return "alarm wake";
    if (r & SYS_RST_12V) return "1.2V";
    if (r & SYS_RST_VCM) return "VCM";
    return "unknown";
}

void health_bridge_alive(void) { bridge_beats++; }

void health_check(void)
{
    u32 b = bridge_beats;
    if (b != seen_beats) { seen_beats = b; stale_ms = 0; return; }
    stale_ms += 1000;
    if (stale_ms >= 5000) {
        log_error("bridge loop stalled: restarting");
        h.hangs++;
        store();
        cpu_reset();
    }
}

void health_reset_counters(void)
{
    u32 reason = h.last_reason;
    memset(&h, 0, sizeof h);
    h.boots = 1;
    h.last_reason = reason;
    store();
}
