/* SPDX-License-Identifier: MIT */
/* Settings editor over USB-MIDI SysEx, for web/editor.html (Web MIDI).
 *
 *   F0 7D 'F' 'M' '1' cmd [payload] F7        7D = non-commercial manufacturer id
 *   0x01 info      -> 0x41 + ASCII "identity;version;settings_version;settings_size"
 *   0x02 get       -> 0x42 + pack7(fm1_settings)
 *   0x03 set + pack7(fm1_settings) -> 0x43 + status (0 ok, 1 bad size, 2 bad magic, 3 bad version)
 *   0x04 status    -> 0x44 + pack7(editor_status)
 *   0x05 presets   -> 0x45 + one byte per slot (1 = used)
 *   0x06 + slot    save the current settings to the slot      -> 0x46 + status
 *   0x07 + slot    load the slot (applied at once)            -> 0x47 + status
 *   0x08 + slot    clear the slot                              -> 0x48 + status
 *   0x09 + port + pack7(midi bytes)   send those bytes straight to port 1 = USB, 2 = BLE, 3 = both -> 0x49 + status
 *   0x0A + from (4 x 7 bits, LSB first)  device log from that absolute offset -> 0x4A + pack7(u32 total, u32 from, bytes)
 *                  up to EDITOR_LOG_CHUNK bytes; total is the count ever written, from the offset actually used
 *   0x0B sysinfo   -> 0x4B + pack7(editor_sysinfo): uptime, heap, task stack margins, health counters
 * Payloads are 8-to-7 packed (upd_pack7). The device checks magic, size and
 * version on set, applies the settings at once and writes them a second later.
 */
#pragma once
#include <stdint.h>

#define EDITOR_PREFIX_LEN 5          /* F0 7D 'F' 'M' '1' */
#define EDITOR_WIRE_MAX   (8 + (256 * 8 + 6) / 7)

#define EDITOR_LOG_CHUNK  200u

typedef struct {
    uint32_t uptime_ms, free_heap;
    uint32_t stack_free_bridge, stack_free_ui;     /* words never used, from uxTaskGetStackHighWaterMark; 0 = unknown */
    uint32_t boots, wdt_resets, soft_resets, hangs, last_reason;
    uint8_t  minimal, pad[3];                      /* 1 = the first-install build */
} editor_sysinfo;

struct logring;

typedef struct {
    uint32_t count_in[4], count_out[4];
    uint32_t dropped;
    uint8_t usb_ok, ble_ok, ble_state;   /* ble_state: 0 adv, 1 connected, 2 host */
} editor_status;

int  editor_is_frame(const uint8_t *wire, uint32_t n);       /* starts with the prefix */
int  editor_handle(const uint8_t *wire, uint32_t n);         /* 1 = handled (reply sent) */

/* platform hooks */
int  editor_port_send(const uint8_t *wire, uint32_t n);
void editor_port_apply(void);                                /* settings changed: push them everywhere */
void editor_port_status(editor_status *st);
void editor_port_send_midi(int port, const uint8_t *bytes, uint32_t n);   /* parse and send to a port */
const struct logring *editor_port_log(void);                 /* the device log ring */
void editor_port_sysinfo(editor_sysinfo *si);
