/* SPDX-License-Identifier: MIT */
#include "settings.h"
#include "midi/router.h"
#include "util/crc16.h"
#include <string.h>
#include <stddef.h>

#define WRITE_DELAY_MS 1000u

static fm1_settings cur;
static uint8_t dirty;
static uint32_t dirty_ms;

void settings_defaults(fm1_settings *s)
{
    memset(s, 0, sizeof *s);
    s->magic = FM1_SETTINGS_MAGIC;
    s->version = FM1_SETTINGS_VERSION;
    s->size = (uint8_t)sizeof *s;
    s->routes = ROUTE_DEFAULT;
    s->filters = 0;
    s->usb_enabled = 1;
    s->backlight_idx = BACKLIGHT_30S;
    s->invert = 0;
    s->ble_mode = BLE_MODE_PERIPHERAL;
    s->ble_host_pair = 0;
    s->panic_on_disconnect = 1;
    s->clock_bpm = 120; s->clock_to = 3; s->merge_guard = 1; s->kbd_vel_source = 0; s->ble_host_auto = 1;
    s->kbd_enabled = 1;
    s->kbd_channel = 0;
    s->kbd_octave = 0;
    s->kbd_velocity = 100;
    {
        int i;
        for (i = 0; i < 16; i++) { s->route[i].channels = 0xFFFF; s->route[i].remap = 0; s->route[i].flags = ROUTE_OPT_DEFAULT; }
        for (i = 0; i < 4; i++) { s->input[i].transpose = 0; s->input[i].vel_curve = VEL_LINEAR; s->input[i].vel_fixed = 100; s->input[i].pad = 0; }
        for (i = 0; i < 10; i++) { s->btn[i].type = 0; s->btn[i].number = 0; }
        for (i = 0; i < 27; i++) { s->key[i].type = 0; s->key[i].number = 0; }
        s->btn[BTN_SLOT_PLAY].type = 2; s->btn[BTN_SLOT_PLAY].number = 64;    /* PLAY = sustain-style CC 64, a page-turn default */
    }
}

/* copy the routing-related settings into a router */
void settings_apply_router(const fm1_settings *s, midi_router *r)
{
    int i;
    r->routes = s->routes & ROUTE_VALID_MASK;
    r->filters = s->filters;
    for (i = 0; i < ROUTE_SLOTS && i < 16; i++) {
        r->opt[i].channels = s->route[i].channels ? s->route[i].channels : 0xFFFF;
        r->opt[i].remap = s->route[i].remap > 16 ? 0 : s->route[i].remap;
        r->opt[i].flags = s->route[i].flags;
    }
    for (i = 0; i < MIDI_PORT_COUNT && i < 4; i++) {
        r->in[i].transpose = s->input[i].transpose < -24 ? -24 : s->input[i].transpose > 24 ? 24 : s->input[i].transpose;
        r->in[i].vel_curve = s->input[i].vel_curve < VEL_CURVE_COUNT ? s->input[i].vel_curve : VEL_LINEAR;
        r->in[i].vel_fixed = s->input[i].vel_fixed ? s->input[i].vel_fixed : 100;
    }
}

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

/* decode a stored record into *out (defaults first); 0 = ok, -1 = nothing valid there */
static int load_id(uint16_t id, fm1_settings *out)
{
    uint8_t buf[sizeof(fm1_settings)];
    int n = settings_port_read(id, buf, sizeof buf);
    uint32_t magic;
    uint8_t size;
    settings_defaults(out);
    if (n < 8) return -1;
    memcpy(&magic, buf, 4);
    size = buf[5];
    if (magic != FM1_SETTINGS_MAGIC || size < 8 || size > sizeof buf || n < size) return -1;
    if (crc16_1021(buf, (uint32_t)(size - 2), 0) != rd16(buf + size - 2)) return -1;
    {
        /* keep the known prefix of an older (smaller) layout, defaults for the rest */
        size_t keep = (size_t)size - 2;
        if (keep > offsetof(fm1_settings, crc)) keep = offsetof(fm1_settings, crc);
        memcpy(out, buf, keep);
        out->version = FM1_SETTINGS_VERSION;
        out->size = (uint8_t)sizeof *out;
    }
    return 0;
}

static void seal(fm1_settings *s)
{
    s->magic = FM1_SETTINGS_MAGIC;
    s->version = FM1_SETTINGS_VERSION;
    s->size = (uint8_t)sizeof *s;
    s->crc = (uint16_t)crc16_1021((const uint8_t *)s, offsetof(fm1_settings, crc), 0);
}

void settings_init(void)
{
    load_id(FM1_SETTINGS_ID, &cur);
    dirty = 0;
}

int settings_preset_used(int slot)
{
    fm1_settings tmp;
    if (slot < 0 || slot >= FM1_PRESET_COUNT) return 0;
    return load_id((uint16_t)FM1_PRESET_ID(slot), &tmp) == 0;
}

int settings_preset_save(int slot)
{
    fm1_settings tmp;
    if (slot < 0 || slot >= FM1_PRESET_COUNT) return -1;
    tmp = cur;
    seal(&tmp);
    return settings_port_write((uint16_t)FM1_PRESET_ID(slot), &tmp, sizeof tmp) == (int)sizeof tmp ? 0 : -1;
}

int settings_preset_load(int slot)
{
    fm1_settings tmp;
    uint8_t host_peer[7];
    if (slot < 0 || slot >= FM1_PRESET_COUNT) return -1;
    if (load_id((uint16_t)FM1_PRESET_ID(slot), &tmp)) return -1;
    memcpy(host_peer, cur.host_peer, 7);                   /* the pairing memory is not part of a preset */
    cur = tmp;
    memcpy(cur.host_peer, host_peer, 7);
    settings_mark_dirty();
    return 0;
}

int settings_preset_peek(int slot, fm1_settings *out)
{
    if (slot < 0 || slot >= FM1_PRESET_COUNT) return -1;
    return load_id((uint16_t)FM1_PRESET_ID(slot), out) ? -1 : 0;
}

int settings_preset_clear(int slot)
{
    uint8_t zero[8] = {0};
    if (slot < 0 || slot >= FM1_PRESET_COUNT) return -1;
    return settings_port_write((uint16_t)FM1_PRESET_ID(slot), zero, sizeof zero) > 0 ? 0 : -1;
}

fm1_settings *settings_get(void) { return &cur; }

void settings_mark_dirty(void)
{
    dirty = 1;
    dirty_ms = settings_port_now_ms();
}

int settings_dirty(void) { return dirty; }

static void write_now(void)
{
    seal(&cur);
    settings_port_write(FM1_SETTINGS_ID, &cur, sizeof cur);
    dirty = 0;
}

void settings_poll(void)
{
    if (dirty && settings_port_now_ms() - dirty_ms >= WRITE_DELAY_MS)
        write_now();
}

void settings_reset(void)
{
    settings_defaults(&cur);
    write_now();
}
