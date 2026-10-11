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

enum { MIDI_PORT_TRS = 0, MIDI_PORT_USB = 1, MIDI_PORT_BLE = 2, MIDI_PORT_KBD = 3, MIDI_PORT_COUNT = 4 };
/* KBD: the FM-1's own keys and knobs (app/midi/keys.c), an input-only port */

#define ROUTE_BIT(src, dst) (1u << ((src) * MIDI_PORT_COUNT + (dst)))
#define ROUTE_TRS_BLE ROUTE_BIT(MIDI_PORT_TRS, MIDI_PORT_BLE)
#define ROUTE_TRS_USB ROUTE_BIT(MIDI_PORT_TRS, MIDI_PORT_USB)
#define ROUTE_USB_BLE ROUTE_BIT(MIDI_PORT_USB, MIDI_PORT_BLE)
#define ROUTE_BLE_USB ROUTE_BIT(MIDI_PORT_BLE, MIDI_PORT_USB)
#define ROUTE_USB_TRS ROUTE_BIT(MIDI_PORT_USB, MIDI_PORT_TRS)   /* no TRS out on the FM-1: never used */
#define ROUTE_BLE_TRS ROUTE_BIT(MIDI_PORT_BLE, MIDI_PORT_TRS)
#define ROUTE_KBD_USB ROUTE_BIT(MIDI_PORT_KBD, MIDI_PORT_USB)
#define ROUTE_KBD_BLE ROUTE_BIT(MIDI_PORT_KBD, MIDI_PORT_BLE)
#define ROUTE_DEFAULT (ROUTE_TRS_BLE | ROUTE_TRS_USB | ROUTE_USB_BLE | ROUTE_BLE_USB | ROUTE_KBD_USB | ROUTE_KBD_BLE)
/* routes that can exist on this hardware (TRS and KBD have no output) */
#define ROUTE_VALID_MASK (ROUTE_TRS_BLE | ROUTE_TRS_USB | ROUTE_USB_BLE | ROUTE_BLE_USB | ROUTE_KBD_USB | ROUTE_KBD_BLE)

/* filters applied in router_feed() before counting, tapping and routing */
#define FILTER_DROP_CLOCK        0x01   /* F8 */
#define FILTER_DROP_ACTIVE_SENSE 0x02   /* FE */
#define FILTER_DROP_SYSEX        0x04

/* per-route options (indexed by ROUTE_INDEX(src, dst)) */
typedef struct {
    uint16_t channels;      /* bit n = channel n+1 passes; 0xFFFF = all */
    uint8_t  remap;         /* 0 = keep, 1..16 = force this channel on channel messages */
    uint8_t  flags;         /* ROUTE_OPT_* */
} route_opt;
#define ROUTE_OPT_CLOCK   0x01   /* pass F8 clock (and FA/FB/FC transport) */
#define ROUTE_OPT_SYSEX   0x02   /* pass SysEx */
#define ROUTE_OPT_DEFAULT (ROUTE_OPT_CLOCK | ROUTE_OPT_SYSEX)
#define ROUTE_INDEX(src, dst) ((src) * MIDI_PORT_COUNT + (dst))
#define ROUTE_SLOTS (MIDI_PORT_COUNT * MIDI_PORT_COUNT)

/* per-input transform (applied once, before fan-out) */
enum { VEL_LINEAR = 0, VEL_SOFT, VEL_HARD, VEL_FIXED, VEL_CURVE_COUNT };
typedef struct {
    int8_t  transpose;      /* semitones, -24..24, notes only */
    uint8_t vel_curve;      /* VEL_* */
    uint8_t vel_fixed;      /* for VEL_FIXED */
} input_opt;

typedef void (*router_sink)(void *ctx, const midi_msg *m);

typedef struct midi_router {
    uint16_t routes;                        /* ROUTE_BIT mask */
    uint8_t filters;                        /* FILTER_* mask */
    uint32_t count_dropped;                 /* messages removed by the filters */
    route_opt opt[ROUTE_SLOTS];
    input_opt in[MIDI_PORT_COUNT];
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
route_opt *router_opt(midi_router *r, int src, int dst);    /* NULL if not a valid route */
void router_opt_defaults(route_opt *o);
uint8_t midi_apply_velocity(uint8_t vel, uint8_t curve, uint8_t fixed);
void router_set_tap(midi_router *r, void (*fn)(void *ctx, int src, const midi_msg *m), void *ctx);
const char *router_port_name(int port);
