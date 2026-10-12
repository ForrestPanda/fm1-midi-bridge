/* SPDX-License-Identifier: MIT */
/* The update entry (step 1) against a hostile host. Two modes, chosen by bit 7 of
 * the first input byte:
 *   clear: the input is cut into frames (one length byte, then bytes; wrapped as
 *          F0 .. F7, raw or pack7) that the port hands to fm1_update_poll() in order;
 *   set:   cooperative host: the harness itself sends the handshake and the upgrade
 *          command and answers the device's read requests from the real package
 *          (build/fm1bridge.fwsc, or a synthetic UFW when absent) after applying the
 *          input as patches (3-byte offset + value; offset bit 23 truncates the
 *          image there instead; a value with bit 0 at offset 0xFFFFFF makes the host
 *          refuse the success checkpoint). That reaches ufw_find, the staging, the
 *          verify-back and the record.
 * The clock jumps a second per idle call so a session always times out. Flash is a
 * RAM image; a write outside the staging area aborts, which is the bug we look for. */
#include "../../app/update/fm1_update.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

static const uint8_t *in; static size_t in_len, in_pos;
static uint8_t frame[UPD_WIRE_MAX]; static uint32_t frame_len;
static uint32_t clock_ms;
static uint8_t flash[0x100000];
static int coop, refuse_success;
static uint8_t *pristine; static size_t pristine_len;      /* logical package image */
static uint8_t *image; static size_t image_len;            /* patched copy for this run */

/* ---- hostile frames from the input (mode 0) ---- */
static void next_frame(void)
{
    uint8_t body[256]; size_t n;
    frame_len = 0;
    if (in_pos >= in_len) return;
    n = in[in_pos++] & 0x7F;
    if (n > in_len - in_pos) n = in_len - in_pos;
    memcpy(body, in + in_pos, n); in_pos += n;
    if (in_pos < in_len && (in[in_pos] & 1)) {
        frame[0] = 0xF0; frame_len = 1 + upd_pack7(body, (uint32_t)n, frame + 1); frame[frame_len++] = 0xF7;
    } else {
        frame[0] = 0xF0; memcpy(frame + 1, body, n); frame_len = (uint32_t)(1 + n); frame[frame_len++] = 0xF7;
    }
    if (in_pos < in_len) in_pos++;
}

/* ---- cooperative host (mode 1) ---- */
static void queue_body(const uint8_t *body, uint32_t n)
{
    frame[0] = 0xF0; frame_len = 1 + upd_pack7(body, n, frame + 1); frame[frame_len++] = 0xF7;
}

static void host_response(uint32_t addr, const uint8_t *data, uint32_t n, uint8_t fl)
{
    uint8_t body[15 + 512]; uint32_t i, s = 0;
    body[0] = 0; body[1] = 0x59; body[2] = 0x30;
    body[3] = (uint8_t)(n + 8); body[4] = (uint8_t)((n + 8) >> 8); body[5] = 0; body[6] = fl;
    body[7] = (uint8_t)addr; body[8] = (uint8_t)(addr >> 8); body[9] = (uint8_t)(addr >> 16); body[10] = (uint8_t)(addr >> 24);
    body[11] = (uint8_t)n; body[12] = (uint8_t)(n >> 8); body[13] = 0;
    memcpy(body + 14, data, n);
    for (i = 6; i < 14 + n; i++) s += body[i];
    body[14 + n] = (uint8_t)~s;
    queue_body(body, 15 + n);
}

static int coop_step;      /* 0: send handshake, 1: send upgrade, 2: answering reads */
static void coop_next(void)
{
    static const uint8_t hs[] = {0x00, 0x59, 0x11, 0x00, 0x00, 0x00, 0xFF};
    static const uint8_t up[] = {0xF0, 0x22, 0x24, 0x35, 0x7F, 0xF7};
    frame_len = 0;
    if (coop_step == 0) { queue_body(hs, sizeof hs); coop_step = 1; }
    else if (coop_step == 1) { memcpy(frame, up, sizeof up); frame_len = sizeof up; coop_step = 2; }
}

int upd_port_send(const uint8_t *wire, uint32_t n)
{
    uint8_t d[64]; uint32_t len;
    if (!coop || n < 3) return 0;
    len = upd_unpack7(wire + 1, n - 2, d, sizeof d);
    if (len == 15 && d[2] == 0x30) {
        uint32_t addr = d[7] | d[8] << 8 | d[9] << 16 | (uint32_t)d[10] << 24;
        uint32_t rl = d[11] | d[12] << 8 | d[13] << 16;
        if (addr == 0xE0000000u || addr == 0xF0000000u) { host_response(addr, (const uint8_t *)(refuse_success ? "failure" : "success"), 8, 0); return 0; }
        if (rl > 512 || addr >= image_len) { host_response(addr, d, 0, d[6]); return 0; }     /* short answer */
        if (addr + rl > image_len) rl = (uint32_t)(image_len - addr);
        host_response(addr, image + addr, rl, d[6]);
    }
    return 0;
}

int upd_port_frame_get(const uint8_t **p, uint32_t *n)
{
    if (!frame_len) { if (coop) coop_next(); else next_frame(); }
    if (!frame_len) return 0;
    *p = frame; *n = frame_len; return 1;
}
void upd_port_frame_done(void) { frame_len = 0; }
uint32_t upd_port_now_ms(void) { return clock_ms; }
void upd_port_idle(void) { clock_ms += 1000; }
int upd_port_flash_erase(uint32_t off) { if (off & 0xFFF) abort(); if (off + 0x1000 > sizeof flash) abort(); memset(flash + off, 0xFF, 0x1000); return 0; }
int upd_port_flash_prog(uint32_t off, const void *p, uint32_t n) { if (off + n > sizeof flash || off < UPD_OTA_AREA || off + n > UPD_OTA_AREA + UPD_OTA_AREA_LEN) abort(); memcpy(flash + off, p, n); return 0; }
int upd_port_flash_read(uint32_t off, void *p, uint32_t n) { if (off + n > sizeof flash) abort(); memcpy(p, flash + off, n); return 0; }
void upd_port_show(uint32_t step, int32_t code) { (void)step; (void)code; }
void upd_port_commit(const uint8_t *parm) { (void)parm; }
void upd_port_uboot(void) {}

/* ---- package: the real one, or a small synthetic UFW with the same shape ---- */
static void load_package(void)
{
    static const char *paths[] = {"../../build/fm1bridge.fwsc", "build/fm1bridge.fwsc", "../build/fm1bridge.fwsc"};
    size_t i;
    for (i = 0; i < sizeof paths / sizeof paths[0] && !pristine; i++) {
        FILE *f = fopen(paths[i], "rb"); uint8_t *raw; long n; size_t k, o = 0;
        if (!f) continue;
        fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
        raw = malloc((size_t)n); if (fread(raw, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(raw); continue; }
        fclose(f);
        pristine = malloc((size_t)n);
        for (k = 0; k < 20; k++) { memcpy(pristine + o, raw + k * 48, 47); o += 47; }
        memcpy(pristine + o, raw + 960, (size_t)n - 960); o += (size_t)n - 960;
        pristine_len = o; free(raw);
    }
    if (!pristine) {                                      /* no build: 64 KB of noise with a UFW-looking start */
        pristine_len = 0x10000; pristine = malloc(pristine_len);
        for (i = 0; i < pristine_len; i++) pristine[i] = (uint8_t)(i * 2654435761u >> 13);
        memcpy(pristine, "UFW", 3);
    }
    image = malloc(pristine_len);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t n)
{
    int guard = 64; size_t i;
    if (!pristine) load_package();
    in = data; in_len = n; in_pos = 0; frame_len = 0; clock_ms = 0; coop_step = 0; refuse_success = 0;
    coop = n > 0 && (data[0] & 0x80);
    memset(flash, 0xFF, sizeof flash);
    if (coop) {
        memcpy(image, pristine, pristine_len); image_len = pristine_len;
        for (i = 1; i + 4 <= n; i += 4) {
            uint32_t off = data[i] | data[i + 1] << 8 | (uint32_t)(data[i + 2] & 0x7F) << 16;
            if (off == 0x7FFFFF) { refuse_success = data[i + 3] & 1; continue; }
            if (data[i + 2] & 0x80) { if (off < image_len) image_len = off; continue; }
            if (off < image_len) image[off] = data[i + 3];
        }
        in_pos = n;
    }
    fm1_update_boot_cleanup();
    while (guard-- && fm1_update_poll());
    return 0;
}

const uint8_t *fuzz_seed(int i, size_t *n)
{
    /* hostile: handshake, upgrade command, soft key, a read response */
    static const uint8_t s0[] = {7, 0x00, 0x59, 0x11, 0x00, 0x00, 0x00, 0xFF, 1,
                                 4, 0x22, 0x24, 0x35, 0x7F, 0,
                                 4, 0x22, 0x24, 0x35, 0x7D, 0,
                                 23, 0x00, 0x59, 0x30, 16, 0, 0, 0, 0, 0, 0, 0, 8, 0, 0, 's', 'u', 'c', 'c', 'e', 's', 's', 0, 0x00, 1};
    /* cooperative: clean package; one byte of the UFW header changed; truncated; success refused */
    static const uint8_t s1[] = {0x80};
    static const uint8_t s2[] = {0x80, 0x10, 0x00, 0x00, 0xA5};
    static const uint8_t s3[] = {0x80, 0x00, 0x20, 0x80, 0x00};
    static const uint8_t s4[] = {0x80, 0xFF, 0xFF, 0x7F, 0x01};
    switch (i) {
    case 0: *n = sizeof s0; return s0;
    case 1: *n = sizeof s1; return s1;
    case 2: *n = sizeof s2; return s2;
    case 3: *n = sizeof s3; return s3;
    case 4: *n = sizeof s4; return s4;
    default: return NULL;
    }
}
