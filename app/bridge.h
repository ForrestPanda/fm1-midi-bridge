/* SPDX-License-Identifier: MIT */
/* The bridge: TRS MIDI IN (UART1), USB MIDI and BLE MIDI through the router. */
#pragma once
#include "typedef.h"
#include "midi/router.h"

void bridge_init(void);                 /* opens the UART, starts the bridge task */
midi_router *bridge_router(void);
int bridge_pump_usb(void);
void bridge_panic_request(void);        /* all sound off + all notes off to USB and BLE */
int  bridge_panic_active(void);              /* drain USB input once; 1 if something was there */       /* for the UI: routes and activity counters */
