/* SPDX-License-Identifier: MIT */
/* The first-install build (make MINIMAL=1): no radio, no key matrix, no UI task.
 * What runs: the bridge task (TRS in, USB MIDI, the updater, the editor, the log)
 * and this small status task, which draws a few lines on the panel, writes
 * settings when the editor changes them and keeps the bridge watchdog.
 * Purpose: prove the install path on hardware with the least code that can hang,
 * and get back to stock from it. Package identity FM-1_930. */
#include "system/includes.h"
#include "app_config.h"
#include "os/os_api.h"
#include "settings.h"
#include "bridge.h"
#include "health.h"
#include "fm1_log.h"
#include "usb/usb_midi.h"
#include "midi/router.h"
#if FM1_MINIMAL_LCD
#include "ui/lcd.h"
#endif

extern u32 usb_otg_online(const usb_dev id);

#if FM1_MINIMAL_LCD
static void line(int row, const char *s)
{
    char buf[31];
    int n = snprintf(buf, sizeof buf, "%-30s", s);
    (void)n;
    lcd_text(0, (u16)row, buf, 0xFFFF, 0x0000);
}
#endif

static void minimal_task(void *p)
{
    u32 n = 0;
#if FM1_MINIMAL_LCD
    int safe = health_get()->unclean >= HEALTH_SAFE_BOOTS;              /* the panel guard, as ui.c */
    int lcd_ok = !safe && lcd_init() == 0;
    if (safe) fm1_log("safe boot: %u boots in a row never ran clean, the panel is left alone", (unsigned)health_get()->unclean);
    fm1_log("lcd init %s", lcd_ok ? "ok" : "FAILED");
    if (lcd_ok) {
        lcd_fill(0, 0, 240, 240, 0x0000);
        line(0, " FM-1 MIDI bridge  MINIMAL");
        line(1, " version " FM1_FW_VERSION);
        line(2, " " __DATE__);
        line(4, " Install build: USB MIDI,");
        line(5, " TRS in, updater, editor.");
        line(6, " No Bluetooth, no keys.");
    }
#endif
    (void)p;
    for (;;) {
        settings_poll();
        if ((n % 100) == 0) {                    /* every 1 s */
            health_check();
            fm1_log_stack_note(FM1_TASK_UI);
#if FM1_MINIMAL_LCD
            if (lcd_ok) {
                char b[40];
                const midi_router *r = bridge_router();
                const fm1_health *h = health_get();
                snprintf(b, sizeof b, " USB: %s", usb_otg_online(0) == SLAVE_MODE ? (usb_midi_configured(0) ? "ready" : "cable") : "no cable");
                line(8, b);
                snprintf(b, sizeof b, " TRS in %u  USB in %u", (unsigned)r->count_in[MIDI_PORT_TRS], (unsigned)r->count_in[MIDI_PORT_USB]);
                line(9, b);
                snprintf(b, sizeof b, " USB out %u", (unsigned)r->count_out[MIDI_PORT_USB]);
                line(10, b);
                snprintf(b, sizeof b, " boot %u  %s", (unsigned)h->boots, health_reason_text(h->last_reason));
                line(12, b);
                snprintf(b, sizeof b, " wdt %u hangs %u", (unsigned)h->wdt_resets, (unsigned)h->hangs);
                line(13, b);
                snprintf(b, sizeof b, " up %u s", (unsigned)(timer_get_ms() / 1000));
                line(14, b);
            }
#endif
        }
        n++;
        if (n == 2000) health_mark_clean();      /* 20 s up: not a crash loop */
        os_time_dly(1);                          /* 10 ms */
    }
}

void minimal_init(void)
{
    os_task_create(minimal_task, NULL, 8, 768, 0, "minimal");
}
