/* SPDX-License-Identifier: MIT */
#include "router.h"
#include <string.h>

void router_init(midi_router *r, uint16_t routes)
{
    memset(r, 0, sizeof *r);
    r->routes = routes & ROUTE_VALID_MASK;
}

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
    r->count_in[src]++;
    if (r->tap) r->tap(r->tap_ctx, src, m);
    for (dst = 0; dst < MIDI_PORT_COUNT; dst++) {
        if (dst == src || !r->sink[dst]) continue;
        if (!(r->routes & ROUTE_BIT(src, dst))) continue;
        r->sink[dst](r->sink_ctx[dst], m);
        r->count_out[dst]++;
    }
}

const char *router_port_name(int port)
{
    static const char *const names[MIDI_PORT_COUNT] = {"TRS", "USB", "BLE"};
    return (port >= 0 && port < MIDI_PORT_COUNT) ? names[port] : "?";
}

void router_set_tap(midi_router *r, void (*fn)(void *ctx, int src, const midi_msg *m), void *ctx)
{
    r->tap = fn;
    r->tap_ctx = ctx;
}
