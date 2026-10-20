/* SPDX-License-Identifier: MIT */
/* The "USB MIDI" switch: when off, a plugged cable charges but the device does not
 * enumerate. The SDK's plug handler (task_pc.c) is gated in app_main.c. */
#include "system/includes.h"
#include "app_config.h"
#include "usb/usb_config.h"
#include "usb/device/usb_stack.h"
#include "settings.h"

extern void usb_start(const usb_dev id);
extern void usb_stop(const usb_dev id);
extern u32 usb_otg_online(const usb_dev id);

int usb_port_enabled(void) { return settings_get()->usb_enabled; }

/* called by the UI after the setting changed */
void usb_port_apply(void)
{
    static u8 applied = 1;                       /* USB starts enabled at boot */
    u8 want = settings_get()->usb_enabled ? 1 : 0;
    if (want == applied) return;                 /* a settings write with no change must not restart USB */
    applied = want;
    if (want) {
        if (usb_otg_online(0) == SLAVE_MODE) usb_start(0);
    } else {
        usb_stop(0);
    }
}
