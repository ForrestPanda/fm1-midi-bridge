/* SPDX-License-Identifier: MIT */
/* MIDI logger screen: a status line and the last messages seen on any port.
 *   row 0      "USB:ok BLE:--  R:TB TU UB BU"   (connections and enabled routes)
 *   rows 1..16 "TRS 90 3C 64" etc., newest at the bottom
 * midilog_add() is called by the bridge for every message (any task context,
 * it only writes into a ring); midilog_draw() renders what changed, from the
 * bridge task, and is cheap when nothing changed. */
#pragma once
#include "typedef.h"
#include "midi/midi_msg.h"
#include "midi/router.h"

void midilog_init(void);
void midilog_add(int port, const midi_msg *m);
void midilog_add_out(int port, const midi_msg *m);   /* something we sent directly to a port (composer) */
void midilog_set_decoded(int on);                    /* raw bytes or decoded text */
int  midilog_decoded(void);
void midilog_status(const char *s);         /* replaces the status line */
void midilog_draw(void);                    /* call periodically, from the UI task only */
void midilog_invalidate(void);              /* redraw everything next time */
void midilog_status_from(const midi_router *r, int usb_ok, int ble_ok);
