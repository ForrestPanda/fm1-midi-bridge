/* SPDX-License-Identifier: MIT */
/* Simulator around app/bridge.c for the host: the three ports, the clock, the task
 * API, settings storage, the update hook, the editor port and the log, all in RAM.
 * tests/test_bridge.c drives it with known traffic, tests/fuzz/fuzz_bridge.c with
 * mutated traffic. Nothing here runs on the FM-1. */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "midi/midi_msg.h"
#include "midi/blemidi.h"

#define SIM_USB_OUT_MAX   16384u         /* bytes of USB-MIDI packets kept: a whole panic (2080 x 4) fits */
#define SIM_BLE_OUT_MAX   64             /* BLE packets kept */
#define SIM_TRS_MAX       4096u

typedef struct {
    uint32_t ms;                         /* timer_get_ms() */
    int usb_configured, ble_ready, ble_full;     /* ble_full: the stack refuses every send */
    uint16_t ble_max;                    /* ble_port_max_packet() */
    int uart_fail;                       /* dev_open("uart1") returns NULL when set before bridge_init */
    uint8_t trs[SIM_TRS_MAX]; uint32_t trs_len, trs_pos;   /* bytes dev_read hands out */
    uint8_t usb_out[SIM_USB_OUT_MAX]; uint32_t usb_out_len; uint32_t usb_out_dropped;
    uint8_t ble_out[SIM_BLE_OUT_MAX][BLEMIDI_MAX_PACKET]; uint16_t ble_out_len[SIM_BLE_OUT_MAX]; int ble_out_n;
    uint8_t upd_frame[1024]; uint32_t upd_frame_len; int upd_frames;    /* last frame handed to the update entry */
    uint8_t editor_reply[1024]; uint32_t editor_reply_len; int editor_replies;
    int update_polls, boot_cleanups, alive, midilog_in, midilog_out, task_created;
} bridge_sim;

extern bridge_sim sim;

void sim_reset(void);                                    /* fresh state and default settings; call before bridge_init */
void sim_trs_push(const uint8_t *bytes, size_t n);       /* arrives on UART1 */
void sim_usb_push(const uint8_t *pkts, uint32_t n);      /* arrives on bulk OUT (n a multiple of 4) */
void sim_ble_push(const uint8_t *pkt, uint16_t len);     /* a write to the MIDI characteristic */
void sim_clear_out(void);                                /* forget captured output */
