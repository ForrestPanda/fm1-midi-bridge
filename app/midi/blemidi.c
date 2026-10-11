/* SPDX-License-Identifier: MIT */
#include "blemidi.h"
#include <string.h>

/* ----------------------------------------------------------------- encoder */

static void enc_reset(blemidi_enc *e)
{
    e->len = 0;
    e->last_status = 0;
}

void blemidi_enc_init(blemidi_enc *e, uint16_t max)
{
    memset(e, 0, sizeof *e);
    blemidi_enc_set_max(e, max);
}

void blemidi_enc_set_max(blemidi_enc *e, uint16_t max)
{
    if (max > BLEMIDI_MAX_PACKET) max = BLEMIDI_MAX_PACKET;
    if (max < 5) max = 5;             /* header + ts + 3-byte message */
    e->max = max;
}

int blemidi_enc_empty(const blemidi_enc *e)
{
    return e->len == 0;
}

static void put_header(blemidi_enc *e, uint16_t ts13)
{
    e->buf[e->len++] = (uint8_t)(0x80u | ((ts13 >> 7) & 0x3Fu));
}

static void put_ts(blemidi_enc *e, uint16_t ts13)
{
    e->buf[e->len++] = (uint8_t)(0x80u | (ts13 & 0x7Fu));
    e->last_ts = ts13;
}

int blemidi_enc_add(blemidi_enc *e, uint32_t ts_ms, const midi_msg *m)
{
    uint16_t ts13 = (uint16_t)(ts_ms & 0x1FFFu);
    uint16_t need;
    uint8_t hdr = e->len == 0;
    uint8_t rs;                       /* running status applies */

    if (m->len == 0) return 1;

    if (m->kind == MIDI_KIND_SYSEX) {
        /* chunk bytes: F0 (with ts) ... data ... F7 (with ts). Continuation
         * chunks in a new packet carry only the header and data. */
        uint8_t i;
        uint16_t n = 0;
        for (i = 0; i < m->len; i++)
            n += (m->b[i] & 0x80u) ? 2u : 1u;
        need = n + hdr;
        if (e->len + need > e->max) {
            /* a SysEx chunk larger than a packet: split by the caller. We refuse
             * only when nothing of it fits; otherwise take what fits. */
            if (e->len != 0) return 0;
            return -1;
        }
        if (hdr) put_header(e, ts13);
        for (i = 0; i < m->len; i++) {
            if (m->b[i] & 0x80u) put_ts(e, ts13);
            e->buf[e->len++] = m->b[i];
        }
        e->in_sysex = !(m->flags & MIDI_SYSEX_END);
        e->last_status = 0;
        return 1;
    }

    /* Running status: same channel status as the previous message in this
     * packet, and the timestamp unchanged. A realtime byte in between is fine
     * per spec but we keep it simple and only elide after channel messages. */
    rs = m->kind == MIDI_KIND_CHANNEL && e->last_status == m->b[0] && e->last_ts == ts13 && !hdr;
    need = (uint16_t)(hdr + (rs ? 0 : 1) + (rs ? m->len - 1 : m->len));
    if (e->len + need > e->max)
        return 0;
    if (hdr) put_header(e, ts13);
    if (rs) {
        memcpy(e->buf + e->len, m->b + 1, m->len - 1u);
        e->len += m->len - 1u;
    } else {
        put_ts(e, ts13);
        memcpy(e->buf + e->len, m->b, m->len);
        e->len += m->len;
    }
    e->last_status = m->kind == MIDI_KIND_CHANNEL ? m->b[0] : 0;
    return 1;
}

const uint8_t *blemidi_enc_take(blemidi_enc *e, uint16_t *len)
{
    *len = e->len;
    enc_reset(e);
    return e->buf;
}

/* ----------------------------------------------------------------- decoder */

void blemidi_dec_init(blemidi_dec *d, blemidi_dec_cb cb, void *ctx)
{
    memset(d, 0, sizeof *d);
    d->cb = cb;
    d->ctx = ctx;
}

static void dec_emit(blemidi_dec *d, uint16_t ts, uint8_t kind, uint8_t flags, const uint8_t *b, uint8_t len)
{
    midi_msg m;
    m.kind = kind;
    m.flags = flags;
    m.len = len;
    memcpy(m.b, b, len);
    d->cb(d->ctx, ts, &m);
}

static void dec_sx_flush(blemidi_dec *d, uint8_t end)
{
    uint8_t flags = (d->sx_first ? MIDI_SYSEX_START : 0) | (end ? MIDI_SYSEX_END : 0);
    dec_emit(d, d->sx_ts, MIDI_KIND_SYSEX, flags, d->sx, d->sx_len);
    d->sx_first = 0;
    d->sx_len = 0;
}

static void dec_sx_push(blemidi_dec *d, uint8_t b)
{
    if (d->sx_len == MIDI_SYSEX_CHUNK)
        dec_sx_flush(d, 0);
    d->sx[d->sx_len++] = b;
}

int blemidi_dec_feed(blemidi_dec *d, const uint8_t *p, uint16_t n)
{
    uint16_t i = 0;
    uint16_t ts_hi, ts = 0;
    uint8_t status = 0;               /* running status within the packet */

    if (n < 1 || (p[0] & 0xC0u) != 0x80u) goto bad;
    ts_hi = (uint16_t)((p[0] & 0x3Fu) << 7);
    i = 1;

    while (i < n) {
        uint8_t b = p[i];
        if (b & 0x80u) {
            /* At a message boundary every high byte is a timestamp: the spec
             * requires one before each status byte. What follows is either a
             * status, or (running status) data bytes. */
            ts = (uint16_t)(ts_hi | (b & 0x7Fu));
            i++;
            if (i >= n) goto bad;
            b = p[i];
            if (!(b & 0x80u)) {
                if (d->in_sysex) continue;       /* SysEx data after a timestamp: odd but harmless */
                if (status == 0) goto bad;        /* running status without a status */
                continue;                         /* data loop below handles it */
            }
            i++;
            if (b >= 0xF8u) {
                if (d->in_sysex && d->sx_len)     /* keep byte order: flush the open chunk */
                    dec_sx_flush(d, 0);
                dec_emit(d, ts, MIDI_KIND_REALTIME, 0, &b, 1);
                continue;
            }
            if (d->in_sysex) {
                if (b == 0xF7u) {
                    dec_sx_push(d, b);
                    dec_sx_flush(d, 1);
                    d->in_sysex = 0;
                    continue;
                }
                dec_sx_flush(d, 1);             /* cut by another status */
                d->in_sysex = 0;
            }
            if (b == 0xF0u) {
                d->in_sysex = 1;
                d->sx_first = 1;
                d->sx_len = 0;
                d->sx_ts = ts;
                d->sx[d->sx_len++] = 0xF0u;
                status = 0;
                continue;
            }
            if (b == 0xF7u) continue;
            {
                uint8_t need = midi_status_len(b);
                uint8_t out[3];
                if (i + need > n) goto bad;
                out[0] = b;
                if (need >= 1) out[1] = p[i];
                if (need == 2) out[2] = p[i + 1];
                if ((need >= 1 && (out[1] & 0x80u)) || (need == 2 && (out[2] & 0x80u))) goto bad;
                i += need;
                if (b < 0xF0u) {
                    status = b;
                    dec_emit(d, ts, MIDI_KIND_CHANNEL, 0, out, (uint8_t)(need + 1));
                } else {
                    status = 0;
                    dec_emit(d, ts, MIDI_KIND_COMMON, 0, out, (uint8_t)(need + 1));
                }
            }
            continue;
        }
        /* data byte: SysEx continuation or running status */
        if (d->in_sysex) {
            dec_sx_push(d, b);
            i++;
            continue;
        }
        if (status == 0) goto bad;
        {
            uint8_t need = midi_status_len(status);
            uint8_t out[3];
            if (i + need > n) goto bad;
            out[0] = status;
            out[1] = p[i];
            if (need == 2) { out[2] = p[i + 1]; if (out[2] & 0x80u) goto bad; }
            i += need;
            dec_emit(d, ts, MIDI_KIND_CHANNEL, 0, out, (uint8_t)(need + 1));
        }
    }
    /* a SysEx still open at the end of the packet: push what we have so the
     * caller can stream it, keep the state for the next packet */
    if (d->in_sysex && d->sx_len)
        dec_sx_flush(d, 0);
    return 0;
bad:
    d->in_sysex = 0;
    d->sx_len = 0;
    return -1;
}
