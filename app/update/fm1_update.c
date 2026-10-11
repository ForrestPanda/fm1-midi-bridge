/* SPDX-License-Identifier: MIT */
#include "fm1_update.h"
#include "util/crc16.h"
#include <string.h>

/* ------------------------------------------------------------- helpers */

uint32_t upd_crc16(const uint8_t *p, uint32_t n, uint32_t c) { return crc16_1021(p, n, c); }

static void jl_enc(uint8_t *p, uint32_t n)                          /* JieLi header cipher, key 0xFFFF */
{
    uint32_t k = 0xFFFFu;
    while (n--) {
        *p++ ^= (uint8_t)k;
        k = ((k << 1) ^ ((k & 0x8000u) ? 0x1021u : 0u)) & 0xFFFFu;
    }
}

uint32_t upd_pack7(const uint8_t *in, uint32_t n, uint8_t *out)   /* 8-to-7, LSB first */
{
    uint32_t acc = 0, nb = 0, o = 0;
    while (n--) {
        acc |= (uint32_t)*in++ << nb;
        nb += 8;
        while (nb >= 7) {
            out[o++] = (uint8_t)(acc & 0x7Fu);
            acc >>= 7;
            nb -= 7;
        }
    }
    if (nb) out[o++] = (uint8_t)(acc & 0x7Fu);
    return o;
}

uint32_t upd_unpack7(const uint8_t *in, uint32_t n, uint8_t *out, uint32_t max)
{
    uint32_t acc = 0, nb = 0, o = 0;
    while (n--) {
        acc |= (uint32_t)(*in++ & 0x7Fu) << nb;
        nb += 7;
        if (nb >= 8) {
            if (o < max) out[o] = (uint8_t)acc;
            o++;
            acc >>= 8;
            nb -= 8;
        }
    }
    return o;
}

static uint32_t rd16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static uint32_t rd24(const uint8_t *p) { return rd16(p) | (uint32_t)p[2] << 16; }
static uint32_t rd32(const uint8_t *p) { return rd24(p) | (uint32_t)p[3] << 24; }
static void wr16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void wr32(uint8_t *p, uint32_t v) { wr16(p, v); wr16(p + 2, v >> 16); }

/* ---------------------------------------------------------------- wire */

static uint8_t dec[UPD_FRAME_MAX];
static uint8_t busy;

static int send_msg(uint32_t cmd, const uint8_t *payload, uint32_t n)   /* n <= 32 */
{
    uint8_t body[6 + 32 + 1], wire[2 + 48];
    uint32_t i, s = 0, w;
    body[0] = 0;
    body[1] = 0x59;
    body[2] = (uint8_t)cmd;
    body[3] = (uint8_t)n;
    body[4] = 0;
    body[5] = 0;
    for (i = 0; i < n; i++) {
        body[6 + i] = payload[i];
        s += payload[i];
    }
    body[6 + n] = (uint8_t)~s;
    wire[0] = 0xF0;
    w = 1 + upd_pack7(body, 7 + n, wire + 1);
    wire[w++] = 0xF7;
    return upd_port_send(wire, w);
}

static void reply_identity(void)
{
    uint8_t id[27];
    uint32_t i;
    for (i = 0; i < sizeof id; i++)
        id[i] = i < sizeof UPD_IDENTITY - 1 ? (uint8_t)UPD_IDENTITY[i] : 0;
    send_msg(0x11, id, sizeof id);
}

/* classify the pending frame: 0 none, 1 raw upgrade, 2 raw soft key, 3 decoded cmd frame (len in *d) */
static int take(uint32_t *d)
{
    const uint8_t *p;
    uint32_t n, s = 0, i, len;
    if (!upd_port_frame_get(&p, &n)) return 0;
    if (n == 6 && p[1] == 0x22 && p[2] == 0x24 && p[3] == 0x35) {
        int k = p[4] == 0x7F ? 1 : p[4] == 0x7D ? 2 : 0;
        upd_port_frame_done();
        return k;
    }
    if (n < 2 || p[0] != 0xF0 || p[n - 1] != 0xF7) { upd_port_frame_done(); return 0; }
    len = upd_unpack7(p + 1, n - 2, dec, sizeof dec);
    upd_port_frame_done();
    if (len < 7 || len > sizeof dec || dec[0] != 0 || dec[1] != 0x59 || rd24(dec + 3) + 7 != len)
        return 0;
    for (i = 6; i + 1 < len; i++) s += dec[i];
    if ((uint8_t)~s != dec[len - 1]) return 0;
    *d = len;
    return 3;
}

static uint32_t deadline;

/* one read request of the logical image, 3 tries x 5 s */
static int read_host(uint32_t addr, uint8_t *dst, uint32_t len)
{
    uint8_t q[8];
    uint32_t tries, t, d;
    if (deadline && (int32_t)(upd_port_now_ms() - deadline) > 0) return -1;
    q[0] = 0;
    wr32(q + 1, addr);
    q[5] = (uint8_t)len;
    q[6] = (uint8_t)(len >> 8);
    q[7] = 0;
    for (tries = 0; tries < 3; tries++) {
        if (send_msg(0x30, q, 8)) return -1;
        for (t = upd_port_now_ms(); upd_port_now_ms() - t < 5000u;) {
            int k;
            upd_port_idle();
            k = take(&d);
            if (k == 0) continue;
            if (k == 3 && d == 7 && dec[2] == 0x11) { reply_identity(); continue; }
            if (k == 3 && d == len + 15 && dec[2] == 0x30 && rd24(dec + 3) == len + 8 &&
                rd32(dec + 7) == addr && rd24(dec + 11) == len) {
                memcpy(dst, dec + 14, len);
                return 0;
            }
        }
    }
    return -1;
}

/* ------------------------------------------------------------- session */

static int ufw_find(uint8_t *hdr, uint32_t *fl_off, uint32_t *fl_len, uint32_t *ota_off, uint32_t *ota_len)
{
    uint32_t i, nent;
    *fl_off = *fl_len = *ota_off = *ota_len = 0;
    if (read_host(0, hdr, 512) || read_host(512, hdr + 512, 512)) return -1;
    jl_enc(hdr, 0x40);
    nent = rd16(hdr + 8);
    if (upd_crc16(hdr + 2, 0x3E, 0) != rd16(hdr) || nent == 0 || nent > 11 ||
        upd_crc16(hdr + 0x40, nent * 0x50, 0) != rd16(hdr + 2))
        return -2;
    for (i = 0; i < nent; i++) {
        uint8_t *e = hdr + 0x40 + i * 0x50;
        jl_enc(e, 0x50);
        if (rd16(e) == 0)   { *fl_off = rd32(e + 8); *fl_len = rd32(e + 12); }   /* flash.bin */
        if (rd16(e) == 100) { *ota_off = rd32(e + 8); *ota_len = rd32(e + 12); } /* ota.bin */
    }
    return 0;
}

static int stage_head(uint32_t len)
{
    static const char NAME[16] = "LOADER.BIN";
    uint8_t h[32];
    memset(h, 0, sizeof h);
    wr16(h + 2, 0xFFFFu);
    wr32(h + 4, UPD_OTA_AREA);
    wr32(h + 8, len + 32);
    h[12] = 0x41;
    h[13] = 0xFF;
    wr16(h + 14, 0xFFFFu);
    memcpy(h + 16, NAME, 16);
    wr16(h, upd_crc16(h + 2, 30, 0));
    return upd_port_flash_prog(UPD_OTA_AREA, h, 32);
}

static void unstage(void)
{
    uint32_t i;
    for (i = 0; i < UPD_OTA_AREA_LEN; i += 0x1000)
        upd_port_flash_erase(UPD_OTA_AREA + i);
}

static int stage(void)
{
    static uint8_t hdr[0x400], b[512];
    uint32_t i, n, ota_off, ota_len, fl_off, fl_len, len, dcrc, crc;
    int rc;
    upd_port_show(1, 0);
    if ((rc = ufw_find(hdr, &fl_off, &fl_len, &ota_off, &ota_len)) != 0) return rc;
    if (!fl_off || !ota_off) return -3;
    /* loader file head (32 bytes) + inner head (32 bytes) */
    if (read_host(ota_off, b, 512)) return -6;
    len = rd32(b + 8);
    if (upd_crc16(b + 2, 30, 0) != rd16(b) || rd32(b + 4) != 0x20 || b[12] != 0x41 ||
        ota_len != len + 0x20 || len > UPD_OTA_AREA_LEN - 0x200 ||
        upd_crc16(b + 0x22, 30, 0) != rd16(b + 0x20) || rd32(b + 0x28) != 0x01C0A800u)
        return -7;
    dcrc = rd16(b + 2);
    /* stage: erase, body, verify from the host's bytes and from flash, head last */
    upd_port_show(3, 0);
    for (i = 0; i < UPD_OTA_AREA_LEN; i += 0x1000)
        if (upd_port_flash_erase(UPD_OTA_AREA + i)) return -8;
    for (i = 0, crc = 0; i < len; i += n) {
        n = len - i > 512 ? 512 : len - i;
        if (read_host(ota_off + 0x20 + i, b, n)) return -9;
        crc = upd_crc16(b, n, crc);
        if (upd_port_flash_prog(UPD_OTA_AREA + 32 + i, b, n)) return -10;
    }
    if (crc != dcrc) return -11;
    for (i = 0, crc = 0; i < len; i += n) {
        n = len - i > 512 ? 512 : len - i;
        if (upd_port_flash_read(UPD_OTA_AREA + 32 + i, b, n)) return -11;
        crc = upd_crc16(b, n, crc);
    }
    if (crc != dcrc) return -11;
    if (stage_head(len)) return -12;
    /* the host confirms; nothing is committed without it */
    upd_port_show(4, 0);
    for (i = 0; i < 4; i++)
        if (!read_host(0xE0000000u, b, 8) && memcmp(b, "success", 8) == 0) return 0;
    return -13;
}

static int session(void)
{
    static uint8_t parm[UPD_PARM_LEN];
    uint8_t back[UPD_PARM_LEN];
    int rc;
    busy = 1;
    deadline = upd_port_now_ms() + 120000u;
    rc = stage();
    if (rc) {
        unstage();
        upd_port_show(9, rc);
        busy = 0;
        return rc;
    }
    /* UPDATA_PARM: type 0x5A0D (the FM-1's loader type), result UPDATA_READY,
     * magic, the loader's identity as file_path, ota_addr = staging area */
    memset(parm, 0, sizeof parm);
    wr16(parm + 2, 0x5A0Du);
    wr16(parm + 4, 0x5A01u);
    wr16(parm + 6, 0x5441u);
    memcpy(parm + 8, "ota-FM-1_015", 12);
    wr32(parm + 72, UPD_OTA_AREA);
    wr16(parm, upd_crc16(parm + 2, 78, 0));
    if (upd_port_flash_prog(UPD_OTA_RES, parm, sizeof parm) ||
        upd_port_flash_read(UPD_OTA_RES, back, sizeof back) || memcmp(back, parm, sizeof parm) != 0) {
        unstage();
        upd_port_show(9, -14);
        busy = 0;
        return -14;
    }
    upd_port_show(5, 0);
    upd_port_commit(parm);      /* never returns on hardware */
    busy = 0;
    return 0;
}

/* ---------------------------------------------------------------- api */

int fm1_update_busy(void) { return busy; }

void fm1_update_boot_cleanup(void)
{
    uint8_t a[4], r[8];
    uint32_t i, used = 0;
    if (upd_port_flash_read(UPD_OTA_AREA, a, 4) || upd_port_flash_read(UPD_OTA_RES, r, 8)) return;
    for (i = 0; i < 4; i++) used |= a[i] != 0xFF;
    for (i = 0; i < 8; i++) used |= r[i] != 0xFF;
    if (used) unstage();
}

int fm1_update_poll(void)
{
    uint32_t d;
    int k = take(&d);
    switch (k) {
    case 1: session(); return 1;
    case 2: upd_port_uboot(); return 1;
    case 3:
        if (d == 7 && dec[2] == 0x11) reply_identity();
        return 1;
    default: return 0;
    }
}
