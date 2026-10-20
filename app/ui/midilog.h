/* SPDX-License-Identifier: MIT */
/* MIDI logger screen: a status line and the last messages seen on any port.
 *   row 0      "USB:ok BLE:--  TB TU UB BU"   (connections and enabled routes; "PAUSED"
 *              or "^N" = N lines above the newest, when the view is held back)
 *   rows 1..16 "TRS 90 3C 64" etc., newest at the bottom
 * The ring keeps 64 lines; the screen shows 16 and SELECT scrolls back through the
 * rest. midilog_add() is called by the bridge for every message (any task context,
 * it only writes into the ring); midilog_draw() renders what changed, from the UI
 * task, and is cheap when nothing changed. Kinds hidden by settings.log_hide never enter
 * the ring (the page's FX / SCL / ENV / LFO buttons): showing a kind again lists it from then on. */
#pragma once
#include "typedef.h"
#include "midi/midi_msg.h"
#include "midi/router.h"

#define MIDILOG_RING 64

#define MIDILOG_HIDE_NOTES  0x01            /* note on / off */
#define MIDILOG_HIDE_PC     0x02            /* program change */
#define MIDILOG_HIDE_CC     0x04            /* control change */
#define MIDILOG_HIDE_OTHER  0x08            /* everything else: clock, SysEx, bend, aftertouch, system */

void midilog_init(void);
void midilog_add(int port, const midi_msg *m);
void midilog_add_out(int port, const midi_msg *m);   /* something we sent directly to a port (composer) */
void midilog_set_decoded(int on);                    /* raw bytes or decoded text */
int  midilog_decoded(void);
void midilog_status(const char *s);         /* replaces the status line */
void midilog_draw(void);                    /* call periodically, from the UI task only */
void midilog_invalidate(void);              /* redraw everything next time */
void midilog_status_from(const midi_router *r, int usb_ok, int ble_ok);
void midilog_scroll(int lines);             /* < 0 = older; back at the newest line the view follows again */
void midilog_pause(int on);                 /* hold the view (new lines still enter the ring) */
int  midilog_paused(void);
