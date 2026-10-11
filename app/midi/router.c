/* SPDX-License-Identifier: MIT */
#include "router.h"
#include <string.h>

void router_opt_defaults(route_opt *o)
{
    o->channels = 0xFFFF;
    o->remap = 0;
    o->flags = ROUTE_OPT_DEFAULT;
}

void router_init(midi_router *r, uint16_t routes)
{
    int i;
    memset(r, 0, sizeof *r);
    r->routes = routes & ROUTE_VALID_MASK;
    for (i = 0; i < ROUTE_SLOTS; i++) router_opt_defaults(&r->opt[i]);
}

route_opt *router_opt(midi_router *r, int src, int dst)
{
    if (src < 0 || src >= MIDI_PORT_COUNT || dst < 0 || dst >= MIDI_PORT_COUNT) return NULL;
    if (!(ROUTE_BIT(src, dst) & ROUTE_VALID_MASK)) return NULL;
    return &r->opt[ROUTE_INDEX(src, dst)];
}

uint8_t midi_apply_velocity(uint8_t v, uint8_t curve, uint8_t fixed)
{
    uint32_t x = v;
    if (v == 0) return 0;                                   /* a note off stays a note off */
    switch (curve) {
    case VEL_SOFT:  x = (x * x) / 127; break;               /* quieter: square */
    case VEL_HARD:  x = 127 - ((127 - x) * (127 - x)) / 127; break;   /* louder: inverted square */
    case VEL_FIXED: x = fixed; break;
    default: break;
    }
    if (x < 1) x = 1;
    if (x > 127) x = 127;
    return (uint8_t)x;
}

static int is_note(uint8_t status) { return (status & 0xE0) == 0x80 || (status & 0xF0) == 0xA0; }   /* 8n 9n An */


void router_set_sink(midi_router *r, int port, router_sink fn, void *ctx)
{
    if (port < 0 || port >= MIDI_PORT_COUNT) return;
    r->sink[port] = fn;
    r->sink_ctx[port] = ctx;
}

uint16_t router_set(midi_router *r, int src, int dst, int on)
{
    uint16_t bit;
    if (src < 0 || src >= MIDI_PORT_COUNT || dst < 0 || dst >= MIDI_PORT_COUNT || src == dst)
        return r->routes;
    bit = (uint16_t)ROUTE_BIT(src, dst);
    if (!(bit & ROUTE_VALID_MASK)) return r->routes;
    if (on) r->routes |= bit; else r->routes &= (uint16_t)~bit;
    return r->routes;
}

int router_enabled(const midi_router *r, int src, int dst)
{
    if (src < 0 || src >= MIDI_PORT_COUNT || dst < 0 || dst >= MIDI_PORT_COUNT) return 0;
    return (r->routes & ROUTE_BIT(src, dst)) != 0;
}

void router_feed(midi_router *r, int src, const midi_msg *m)
{
    int dst;
    midi_msg t;                                             /* transformed copy, when needed */
    const midi_msg *in = m;
    if (src < 0 || src >= MIDI_PORT_COUNT) return;
    if (r->filters) {
        int drop = 0;
        if (m->kind == MIDI_KIND_REALTIME) {
            if ((r->filters & FILTER_DROP_CLOCK) && m->b[0] == 0xF8) drop = 1;
            if ((r->filters & FILTER_DROP_ACTIVE_SENSE) && m->b[0] == 0xFE) drop = 1;
        } else if (m->kind == MIDI_KIND_SYSEX && (r->filters & FILTER_DROP_SYSEX)) {
            drop = 1;
        }
        if (drop) { r->count_dropped++; return; }
    }
    /* input transform: transpose and velocity on note messages */
    if (m->kind == MIDI_KIND_CHANNEL && is_note(m->b[0]) && m->len == 3 &&
        (r->in[src].transpose || r->in[src].vel_curve != VEL_LINEAR)) {
        int note = m->b[1] + r->in[src].transpose;
        t = *m;
        if (note < 0 || note > 127) { r->count_dropped++; return; }
        t.b[1] = (uint8_t)note;
        if ((m->b[0] & 0xF0) == 0x90) t.b[2] = midi_apply_velocity(m->b[2], r->in[src].vel_curve, r->in[src].vel_fixed);
        in = &t;
    }
    r->count_in[src]++;
    if (r->tap) r->tap(r->tap_ctx, src, in);
    for (dst = 0; dst < MIDI_PORT_COUNT; dst++) {
        const route_opt *o = &r->opt[ROUTE_INDEX(src, dst)];
        const midi_msg *out = in;
        midi_msg re;
        if (dst == src || !r->sink[dst]) continue;
        if (!(r->routes & ROUTE_BIT(src, dst))) continue;
        if (in->kind == MIDI_KIND_REALTIME && in->b[0] >= 0xF8 && in->b[0] <= 0xFC && !(o->flags & ROUTE_OPT_CLOCK)) continue;
        if (in->kind == MIDI_KIND_SYSEX && !(o->flags & ROUTE_OPT_SYSEX)) continue;
        if (in->kind == MIDI_KIND_CHANNEL) {
            uint8_t ch = in->b[0] & 0x0F;
            if (!(o->channels & (1u << ch))) continue;
            if (o->remap && o->remap - 1 != ch) {
                re = *in;
                re.b[0] = (uint8_t)((in->b[0] & 0xF0) | (o->remap - 1));
                out = &re;
            }
        }
        r->sink[dst](r->sink_ctx[dst], out);
        r->count_out[dst]++;
    }
}

const char *router_port_name(int port)
{
    static const char *const names[MIDI_PORT_COUNT] = {"TRS", "USB", "BLE", "KBD"};
    return (port >= 0 && port < MIDI_PORT_COUNT) ? names[port] : "?";
}

void router_set_tap(midi_router *r, void (*fn)(void *ctx, int src, const midi_msg *m), void *ctx)
{
    r->tap = fn;
    r->tap_ctx = ctx;
}
