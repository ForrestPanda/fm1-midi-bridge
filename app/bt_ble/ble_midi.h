/* SPDX-License-Identifier: Apache-2.0 */
/* BLE MIDI peripheral on the SDK GATT server (ble_midi.c). The SDK glue keeps
 * its public names (bt_ble_init, bt_ble_exit, bt_ble_adv_enable, ...). */
#pragma once
#include "typedef.h"

#define BLE_MIDI_DEVICE_NAME "FM-1 MIDI"

/* packets written by the central arrive here, in the BT stack's task */
void ble_midi_set_rx(void *priv, void (*cb)(void *priv, const u8 *pkt, u16 len));
u16 ble_midi_max_packet(void);     /* bytes one notification may carry (ATT_MTU - 3) */
int ble_midi_ready(void);          /* connected and the central subscribed */
int ble_midi_send(const u8 *pkt, u16 len);   /* 0 = queued */
void ble_midi_role_peripheral(void);
void ble_midi_set_link_mode(int mode);       /* BLE_LINK_* (settings.h) */         /* re-enable the peripheral role (after host mode) */
