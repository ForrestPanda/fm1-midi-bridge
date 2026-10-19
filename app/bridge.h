/* SPDX-License-Identifier: MIT */
/* The bridge: TRS MIDI IN (UART1), USB MIDI and BLE MIDI through the router. */
#pragma once
#include "typedef.h"
#include "midi/router.h"
#include "midi/stats.h"
#include "midi/recorder.h"

void bridge_init(void);                 /* opens the UART, starts the bridge task */
midi_router *bridge_router(void);
int bridge_pump_usb(void);              /* drain USB input once; 1 if something was there */
u32 bridge_ble_drops(void);             /* BLE packets given up on (the stack refused them for 250 ms) */
int bridge_step(void);                  /* one pass of the bridge loop; 1 if anything was done */
void bridge_trs_rx(const u8 *bytes, u32 n);        /* TRS bytes in (the UART reader task; the host test) */
void bridge_inject(int port, const midi_msg *m);   /* from any task: route a message as if it came from port */
void bridge_send_to(int port, const midi_msg *m);  /* from any task: straight to MIDI_PORT_USB or MIDI_PORT_BLE */
struct midi_stats *bridge_stats(void);
void bridge_stats_reset(void);
struct midi_recorder *bridge_recorder(void);    /* the UI drives record/play on it (bridge task plays it) */
void bridge_recorder_ports(u8 mask);            /* bit 0 USB, bit 1 BLE */
void bridge_clock_run(int on);                  /* clock master: start (sends FA) / stop (FC) */
int  bridge_clock_running(void);
void bridge_panic_request(void);        /* all sound off + all notes off to USB and BLE */
int  bridge_panic_active(void);
