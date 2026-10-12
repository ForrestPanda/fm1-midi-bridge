/* SPDX-License-Identifier: MIT */
/* The settings editor: the input is taken both as a whole frame and as the body
 * of a well-formed editor frame (prefix + body + F7), so every command sees
 * arbitrary payloads. The settings store and the hooks are stubs. */
#include "../../app/editor.h"
#include "../../app/settings.h"
#include "../../app/util/logring.h"
#include <string.h>
#include <stdint.h>

static uint8_t store[64][512]; static int store_len[64];
int settings_port_read(uint16_t id, void *buf, uint16_t len) { if (id >= 64 || store_len[id] <= 0) return -1; if (len > store_len[id]) len = (uint16_t)store_len[id]; memcpy(buf, store[id], len); return len; }
int settings_port_write(uint16_t id, const void *buf, uint16_t len) { if (id >= 64 || len > 512) return -1; memcpy(store[id], buf, len); store_len[id] = len; return len; }
uint32_t settings_port_now_ms(void) { return 0; }
int editor_port_send(const uint8_t *wire, uint32_t n) { (void)wire; (void)n; return 0; }
void editor_port_apply(void) {}
void editor_port_status(editor_status *st) { st->usb_ok = 1; }
void editor_port_send_midi(int port, const uint8_t *bytes, uint32_t n) { (void)port; (void)bytes; (void)n; }
static logring ring;
const struct logring *editor_port_log(void) { return &ring; }
void editor_port_sysinfo(editor_sysinfo *si) { si->uptime_ms = 1; }

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t n)
{
    static int inited;
    uint8_t f[EDITOR_WIRE_MAX + 2048 + 8];
    if (!inited) { inited = 1; settings_init(); logring_write(&ring, "boot\n", 5); }
    if (n > 2048) n = 2048;
    if (n) editor_handle(data, (uint32_t)n);
    f[0] = 0xF0; f[1] = 0x7D; f[2] = 'F'; f[3] = 'M'; f[4] = '1';
    memcpy(f + 5, data, n); f[5 + n] = 0xF7;
    editor_handle(f, (uint32_t)(6 + n));
    return 0;
}

const uint8_t *fuzz_seed(int i, size_t *n)
{
    static const uint8_t s0[] = {0x01};
    static const uint8_t s1[] = {0x03, 0x31, 0x46, 0x4D, 0x31, 0x09, 0x54, 0x01};
    static const uint8_t s2[] = {0x09, 0x03, 0x10, 0x3C, 0x64, 0x00};
    static const uint8_t s3[] = {0x0A, 0x05, 0x00, 0x00, 0x00};
    static const uint8_t s4[] = {0x06, 0x01};
    switch (i) {
    case 0: *n = sizeof s0; return s0;
    case 1: *n = sizeof s1; return s1;
    case 2: *n = sizeof s2; return s2;
    case 3: *n = sizeof s3; return s3;
    case 4: *n = sizeof s4; return s4;
    default: return NULL;
    }
}
