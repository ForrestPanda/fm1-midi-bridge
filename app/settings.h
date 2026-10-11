/* SPDX-License-Identifier: MIT */
/* Persistent settings: one versioned, CRC-checked struct in the SDK's syscfg (VM).
 * Read once at boot (settings_init), written from the UI task at most once per second
 * after the last change (settings_poll). Never from an ISR. */
#pragma once
#include <stdint.h>

#define FM1_SETTINGS_ID       10            /* syscfg user id, 1..59 */
#define FM1_PRESET_COUNT      4
#define FM1_PRESET_ID(n)      (11 + (n))    /* preset slots: ids 11..14 */
#define FM1_SETTINGS_MAGIC    0x314D4631u   /* "1FM1" */
#define FM1_SETTINGS_VERSION  9

enum { BLE_MODE_PERIPHERAL = 0, BLE_MODE_HOST = 1 };
enum { BLE_LINK_LOW_LATENCY = 0, BLE_LINK_POWER_SAVING = 1 };
enum { BACKLIGHT_NEVER = 0, BACKLIGHT_10S, BACKLIGHT_30S, BACKLIGHT_1MIN, BACKLIGHT_5MIN, BACKLIGHT_COUNT };

typedef struct {
    uint32_t magic;
    uint8_t  version;
    uint8_t  size;              /* sizeof(fm1_settings) of the writer */
    uint16_t routes;            /* ROUTE_* mask */
    uint8_t  filters;           /* FILTER_* mask */
    uint8_t  usb_enabled;
    uint8_t  backlight_idx;     /* BACKLIGHT_* */
    uint8_t  invert;
    uint8_t  ble_mode;          /* BLE_MODE_* */
    uint8_t  ble_host_pair;     /* host mode: request pairing on connect */
    uint8_t  host_peer[7];      /* addr_type + addr of the last host-mode device, zeros = none */
    uint8_t  ble_link_mode;     /* BLE_LINK_*: connection interval / latency profile */
    uint8_t  kbd_enabled;       /* the FM-1's keys and knobs send MIDI */
    uint8_t  kbd_channel;       /* 0..15 */
    int8_t   kbd_octave;        /* -3..3 */
    uint8_t  kbd_velocity;      /* 1..127 */
    /* v4: per-route options (ROUTE_INDEX order) and per-input transforms */
    struct { uint16_t channels; uint8_t remap; uint8_t flags; } route[16];
    struct { int8_t transpose; uint8_t vel_curve; uint8_t vel_fixed; uint8_t pad; } input[4];
    /* v5: MIDI jobs of the free function buttons, BTN_SLOT_* order */
    struct { uint8_t type; uint8_t number; } btn[10];
    /* v6: a note key may do a job instead of its note (BTN_MAP_NONE = the keyboard note) */
    struct { uint8_t type; uint8_t number; } key[27];
    uint8_t  panic_on_disconnect;   /* v7: all notes off to the other outputs when a link drops */
    uint8_t  pad;
    /* v8: composer favourites: kind, channel, data1, data2, port (COMPOSE_* in compose.h) */
    struct { uint8_t kind, channel, d1, d2, port; } fav[4];
    /* v9 */
    uint16_t clock_bpm;         /* clock master tempo, 20..300 */
    uint8_t  clock_to;          /* 1 USB, 2 BLE, 3 both */
    uint8_t  merge_guard;       /* note-collision guard on the outputs */
    uint8_t  kbd_vel_source;    /* 0 fixed, 1 MASTER pot */
    uint8_t  ble_host_auto;     /* host mode: reconnect to the last device by itself */
    uint8_t  pad2[2];
    uint16_t crc;               /* upd_crc16 over everything before it */
} fm1_settings;                 /* 212 bytes */

/* function buttons that can send MIDI (HOME, GLO, OCT-, OCT+, SAVE and SELECT belong to the UI) */
enum { BTN_SLOT_FX = 0, BTN_SLOT_SCL, BTN_SLOT_ENV, BTN_SLOT_LFO, BTN_SLOT_EDIT, BTN_SLOT_SEQ, BTN_SLOT_ARP, BTN_SLOT_PLAY, BTN_SLOT_REC, BTN_SLOT_COUNT };

struct midi_router;
void settings_apply_router(const fm1_settings *s, struct midi_router *r);
void settings_init(void);               /* load, or defaults */
fm1_settings *settings_get(void);
void settings_defaults(fm1_settings *s);
void settings_mark_dirty(void);         /* something in settings_get() changed */
void settings_poll(void);               /* call periodically from a task: writes when due */
void settings_reset(void);              /* defaults, written at once */
int  settings_dirty(void);
/* presets: whole settings snapshots in their own slots */
int  settings_preset_used(int slot);                     /* 1 if the slot holds a valid snapshot */
int  settings_preset_save(int slot);                     /* current settings -> slot; 0 = ok */
int  settings_preset_load(int slot);                     /* slot -> current settings (caller applies); 0 = ok */
int  settings_preset_clear(int slot);

/* platform hooks (settings_port.c on the SDK, the test on the host) */
int  settings_port_read(uint16_t id, void *buf, uint16_t len);        /* bytes read, <= 0 if none */
int  settings_port_write(uint16_t id, const void *buf, uint16_t len); /* bytes written */
uint32_t settings_port_now_ms(void);
