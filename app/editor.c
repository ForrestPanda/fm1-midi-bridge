/* SPDX-License-Identifier: MIT */
#include "editor.h"
#include "settings.h"
#include "update/fm1_update.h"     /* UPD_IDENTITY */
#include "util/pack7.h"
#include "util/logring.h"
#include <string.h>
#include <stdio.h>

#ifndef FM1_FW_VERSION
#define FM1_FW_VERSION "dev"
#endif

static const uint8_t prefix[EDITOR_PREFIX_LEN] = { 0xF0, 0x7D, 'F', 'M', '1' };

__attribute__((weak)) void editor_port_matrix(uint32_t *ticks, uint32_t *pa, uint32_t *pb, uint8_t *raw, uint32_t ncols)
{
    (void)raw; (void)ncols;
    *ticks = *pa = *pb = 0;
}
__attribute__((weak)) void editor_port_matrix_force(uint32_t word, uint32_t ticks) { (void)word; (void)ticks; }

int editor_is_frame(const uint8_t *wire, uint32_t n)
{
    return n >= EDITOR_PREFIX_LEN + 2 && memcmp(wire, prefix, EDITOR_PREFIX_LEN) == 0;
}

static int reply(uint8_t cmd, const uint8_t *payload, uint32_t n, int packed)
{
    static uint8_t wire[EDITOR_WIRE_MAX];
    uint32_t w;
    memcpy(wire, prefix, EDITOR_PREFIX_LEN);
    wire[EDITOR_PREFIX_LEN] = cmd;
    w = EDITOR_PREFIX_LEN + 1;
    if (packed) w += pack7(payload, n, wire + w);
    else { memcpy(wire + w, payload, n); w += n; }
    wire[w++] = 0xF7;
    return editor_port_send(wire, w);
}

int editor_handle(const uint8_t *wire, uint32_t n)
{
    uint8_t cmd;
    if (!editor_is_frame(wire, n) || wire[n - 1] != 0xF7) return 0;
    cmd = wire[EDITOR_PREFIX_LEN];
    switch (cmd) {
    case 0x01: {
        char info[64];
        int k = snprintf(info, sizeof info, "%s;%s;%d;%d", UPD_IDENTITY, FM1_FW_VERSION, FM1_SETTINGS_VERSION, (int)sizeof(fm1_settings));
        reply(0x41, (const uint8_t *)info, (uint32_t)k, 0);
        return 1;
    }
    case 0x02:
        reply(0x42, (const uint8_t *)settings_get(), sizeof(fm1_settings), 1);
        return 1;
    case 0x03: {
        fm1_settings in;
        uint8_t status = 0;
        uint32_t len = unpack7(wire + EDITOR_PREFIX_LEN + 1, n - EDITOR_PREFIX_LEN - 2, (uint8_t *)&in, sizeof in);
        if (len < sizeof in) status = 1;
        else if (in.magic != FM1_SETTINGS_MAGIC || in.size != sizeof in) status = 2;
        else if (in.version != FM1_SETTINGS_VERSION) status = 3;
        if (status == 0) {
            fm1_settings *cur = settings_get();
            uint8_t host_peer[7];
            memcpy(host_peer, cur->host_peer, 7);          /* the device owns its pairing memory */
            memcpy(cur, &in, sizeof in);
            memcpy(cur->host_peer, host_peer, 7);
            settings_mark_dirty();
            editor_port_apply();
        }
        reply(0x43, &status, 1, 0);
        return 1;
    }
    case 0x04: {
        editor_status st;
        memset(&st, 0, sizeof st);
        editor_port_status(&st);
        reply(0x44, (const uint8_t *)&st, sizeof st, 1);
        return 1;
    }
    case 0x05: {
        uint8_t used[FM1_PRESET_COUNT];
        int i;
        for (i = 0; i < FM1_PRESET_COUNT; i++) used[i] = (uint8_t)settings_preset_used(i);
        reply(0x45, used, sizeof used, 0);
        return 1;
    }
    case 0x06: case 0x07: case 0x08: {
        uint8_t status = 1;
        int slot = n >= EDITOR_PREFIX_LEN + 3 ? wire[EDITOR_PREFIX_LEN + 1] : -1;
        if (slot >= 0 && slot < FM1_PRESET_COUNT) {
            if (cmd == 0x06) status = settings_preset_save(slot) ? 2 : 0;
            else if (cmd == 0x07) { status = settings_preset_load(slot) ? 2 : 0; if (!status) editor_port_apply(); }
            else status = settings_preset_clear(slot) ? 2 : 0;
        }
        reply((uint8_t)(cmd | 0x40), &status, 1, 0);
        return 1;
    }
    case 0x09: {
        uint8_t bytes[128], status = 1;
        uint32_t len;
        if (n >= EDITOR_PREFIX_LEN + 3) {
            int port = wire[EDITOR_PREFIX_LEN + 1];
            len = unpack7(wire + EDITOR_PREFIX_LEN + 2, n - EDITOR_PREFIX_LEN - 3, bytes, sizeof bytes);
            if (len && len <= sizeof bytes && port >= 1 && port <= 3) { editor_port_send_midi(port, bytes, len); status = 0; }
        }
        reply(0x49, &status, 1, 0);
        return 1;
    }
    case 0x0A: {
        uint8_t out[8 + EDITOR_LOG_CHUNK];
        uint32_t from = 0, got, total;
        const logring *lr = editor_port_log();
        if (n >= EDITOR_PREFIX_LEN + 6)
            from = (uint32_t)(wire[EDITOR_PREFIX_LEN + 1] & 0x7F) | (uint32_t)(wire[EDITOR_PREFIX_LEN + 2] & 0x7F) << 7 |
                   (uint32_t)(wire[EDITOR_PREFIX_LEN + 3] & 0x7F) << 14 | (uint32_t)(wire[EDITOR_PREFIX_LEN + 4] & 0x7F) << 21;
        got = logring_read(lr, &from, out + 8, EDITOR_LOG_CHUNK);
        total = logring_total(lr);
        out[0] = (uint8_t)total; out[1] = (uint8_t)(total >> 8); out[2] = (uint8_t)(total >> 16); out[3] = (uint8_t)(total >> 24);
        out[4] = (uint8_t)from;  out[5] = (uint8_t)(from >> 8);  out[6] = (uint8_t)(from >> 16);  out[7] = (uint8_t)(from >> 24);
        reply(0x4A, out, 8 + got, 1);
        return 1;
    }
    case 0x0C: {
        uint8_t out[12 + 24];
        uint32_t ticks = 0, pa = 0, pb = 0;
        memset(out, 0, sizeof out);
        if (n >= EDITOR_PREFIX_LEN + 6)
            editor_port_matrix_force((uint32_t)(wire[EDITOR_PREFIX_LEN + 1] & 0x7F) | (uint32_t)(wire[EDITOR_PREFIX_LEN + 2] & 0x7F) << 7,
                                     (uint32_t)(wire[EDITOR_PREFIX_LEN + 3] & 0x7F) | (uint32_t)(wire[EDITOR_PREFIX_LEN + 4] & 0x7F) << 7);
        editor_port_matrix(&ticks, &pa, &pb, out + 12, 24);
        out[0] = (uint8_t)ticks; out[1] = (uint8_t)(ticks >> 8); out[2] = (uint8_t)(ticks >> 16); out[3] = (uint8_t)(ticks >> 24);
        out[4] = (uint8_t)pa; out[5] = (uint8_t)(pa >> 8); out[6] = (uint8_t)(pa >> 16); out[7] = (uint8_t)(pa >> 24);
        out[8] = (uint8_t)pb; out[9] = (uint8_t)(pb >> 8); out[10] = (uint8_t)(pb >> 16); out[11] = (uint8_t)(pb >> 24);
        reply(0x4C, out, sizeof out, 1);
        return 1;
    }
    case 0x0B: {
        editor_sysinfo si;
        memset(&si, 0, sizeof si);
        editor_port_sysinfo(&si);
        reply(0x4B, (const uint8_t *)&si, sizeof si, 1);
        return 1;
    }
    default:
        return 1;                                            /* ours, but unknown: swallow */
    }
}
