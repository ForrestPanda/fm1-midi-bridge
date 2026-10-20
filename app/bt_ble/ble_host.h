/* SPDX-License-Identifier: MIT */
/* BLE host (central) mode: scan for BLE MIDI devices, connect to one, subscribe to its
 * MIDI characteristic and exchange packets with it. Exclusive with the peripheral role
 * (one link, one role): entering host mode stops advertising and disconnects a phone. */
#pragma once
#include "typedef.h"

enum { HOST_OFF = 0, HOST_STARTING, HOST_IDLE, HOST_SCANNING, HOST_CONNECTING, HOST_DISCOVERING, HOST_READY, HOST_DISCONNECTING };

typedef struct {
    u8 addr_type;
    u8 addr[6];
    s8 rssi;
    u8 used;
    u8 rsp_seen;            /* a scan response of its was logged */
    char name[20];
} ble_host_device;
#define BLE_HOST_MAX_DEVICES 8

void ble_host_enter(void);                  /* switch the stack to the client role and start scanning */
void ble_host_exit(void);                   /* disconnect / stop, then back to the peripheral role */
int  ble_host_active(void);                 /* != HOST_OFF */
int  ble_host_state(void);
void ble_host_scan(int on);
int  ble_host_devices(ble_host_device *out, int max);   /* snapshot of the found devices */
int  ble_host_connect(int index);           /* from the snapshot order */
void ble_host_disconnect(void);
int  ble_host_ready(void);                  /* connected and subscribed */
u16  ble_host_max_packet(void);
int  ble_host_send(const u8 *pkt, u16 len);
void ble_host_set_rx(void *priv, void (*cb)(void *priv, const u8 *pkt, u16 len));
const char *ble_host_state_text(void);
const ble_host_device *ble_host_peer(void);
void ble_host_set_link_mode(int mode);
int  ble_host_connect_addr(u8 addr_type, const u8 *addr);
void ble_host_tick(void);                   /* ~100 ms: reconnection timer */
