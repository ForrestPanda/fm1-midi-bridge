/* SPDX-License-Identifier: MIT */
/* The bridge's single BLE entry point: peripheral (ble_midi.c) or host (ble_host.c),
 * whichever mode is active. */
#pragma once
#include "typedef.h"

void ble_port_set_rx(void *priv, void (*cb)(void *priv, const u8 *pkt, u16 len));
int  ble_port_ready(void);
u16  ble_port_max_packet(void);
int  ble_port_send(const u8 *pkt, u16 len);
void ble_port_set_mode(int mode);           /* BLE_MODE_PERIPHERAL / BLE_MODE_HOST, from the UI */
void ble_port_boot(void);
void ble_port_set_link_mode(int mode);      /* BLE_LINK_*: applied to whichever role is active */                   /* after the stack is up: apply the stored mode */
const char *ble_port_state_text(void);
