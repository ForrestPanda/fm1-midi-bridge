/* SPDX-License-Identifier: MIT */
/* MIDI routing matrix: which input ports feed which output ports.
 *
 * Ports: TRS (MIDI IN jack, input only), USB (device, in and out), BLE (in and out).
 * A route is (source, destination). The default set is every sensible route on:
 *   TRS -> BLE, TRS -> USB, USB -> BLE, BLE -> USB.
 * The matrix is a bitmask stored in a byte so it can live in settings as is.
 * router_feed() delivers one message from a source to every enabled destination
 * through the sink callbacks the application registered. Messages are never
 * echoed back to their source port.
 */
#pragma once
#include "midi_msg.h"

enum { MIDI_PORT_TRS = 0, MIDI_PORT_USB = 1, MIDI_PORT_BLE = 2, MIDI_PORT_COUNT = 3 };

#define ROUTE_BIT(src, dst) (1u << ((src) * MIDI_PORT_COUNT + (dst)))
#define ROUTE_TRS_BLE ROUTE_BIT(MIDI_PORT_TRS, MIDI_PORT_BLE)
#define ROUTE_TRS_USB ROUTE_BIT(MIDI_PORT_TRS, MIDI_PORT_USB)
#define ROUTE_USB_BLE ROUTE_BIT(MIDI_PORT_USB, MIDI_PORT_BLE)
#define ROUTE_BLE_USB ROUTE_BIT(MIDI_PORT_BLE, MIDI_PORT_USB)
#define ROUTE_USB_TRS ROUTE_BIT(MIDI_PORT_USB, MIDI_PORT_TRS)   /* no TRS out on the FM-1: never used */
#define ROUTE_BLE_TRS ROUTE_BIT(MIDI_PORT_BLE, MIDI_PORT_TRS)
#define ROUTE_DEFAULT (ROUTE_TRS_BLE | ROUTE_TRS_USB | ROUTE_USB_BLE | ROUTE_BLE_USB)
/* routes that can exist on this hardware (TRS has no output) */
#define ROUTE_VALID_MASK (ROUTE_TRS_BLE | ROUTE_TRS_USB | ROUTE_USB_BLE | ROUTE_BLE_USB)

/* filters applied in router_feed() before counting, tapping and routing */
#define FILTER_DROP_CLOCK        0x01   /* F8 */
#define FILTER_DROP_ACTIVE_SENSE 0x02   /* FE */
#define FILTER_DROP_SYSEX        0x04

typedef void (*router_sink)(void *ctx, const midi_msg *m);

typedef struct {
    uint16_t routes;                        /* ROUTE_BIT mask */
    uint8_t filters;                        /* FILTER_* mask */
    uint32_t count_dropped;                 /* messages removed by the filters */
    router_sink sink[MIDI_PORT_COUNT];      /* output of each port, NULL if absent */
    void *sink_ctx[MIDI_PORT_COUNT];
    uint32_t count_in[MIDI_PORT_COUNT];     /* activity counters for the UI */
    uint32_t count_out[MIDI_PORT_COUNT];
    void (*tap)(void *ctx, int src, const midi_msg *m);   /* observer: every message fed, before routing */
    void *tap_ctx;
} midi_router;

void router_init(midi_router *r, uint16_t routes);
void router_set_sink(midi_router *r, int port, router_sink fn, void *ctx);
/* set/clear/toggle one route; returns the new mask. Invalid routes are ignored. */
uint16_t router_set(midi_router *r, int src, int dst, int on);
int router_enabled(const midi_router *r, int src, int dst);
void router_feed(midi_router *r, int src, const midi_msg *m);
void router_set_tap(midi_router *r, void (*fn)(void *ctx, int src, const midi_msg *m), void *ctx);
const char *router_port_name(int port);
