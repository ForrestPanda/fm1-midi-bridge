/* SPDX-License-Identifier: MIT */
#include "system/includes.h"
#include "app_config.h"
#include "editor.h"
#include "settings.h"
#include "bridge.h"
#include "fm1_log.h"
#include "health.h"
#include "bt_ble/ble_port.h"
#include "usb/usb_midi.h"
#include "midi/midi_msg.h"
#include "midi/router.h"
#if !FM1_MINIMAL
#include "ui/ui.h"
#include "ui/lcd.h"
#endif

extern int upd_port_send(const uint8_t *wire, uint32_t n);
extern void usb_port_apply(void);
extern u16 bt_ble_is_connected(void);
extern int ble_host_active(void);
extern u32 get_malloc_remain_heap_size(void);   /* system.a (lib/system/mm) */

int editor_port_send(const uint8_t *wire, uint32_t n) { return upd_port_send(wire, n); }

void editor_port_apply(void)
{
    fm1_settings *s = settings_get();
    settings_apply_router(s, bridge_router());
    usb_port_apply();
#if !FM1_MINIMAL
    ui_keys_reload();
    ble_port_set_link_mode(s->ble_link_mode);
    ble_port_set_mode(s->ble_mode);
    lcd_invert(s->invert);
    ui_invalidate();
#endif
}

void editor_port_status(editor_status *st)
{
    midi_router *r = bridge_router();
    int i;
    for (i = 0; i < 4; i++) { st->count_in[i] = r->count_in[i]; st->count_out[i] = r->count_out[i]; }
    st->dropped = r->count_dropped;
    st->usb_ok = (u8)usb_midi_configured(0);
    st->ble_ok = (u8)ble_port_ready();
    st->ble_state = ble_host_active() ? 2 : bt_ble_is_connected() ? 1 : 0;
}

const struct logring *editor_port_log(void) { return fm1_log_ring(); }

void editor_port_sysinfo(editor_sysinfo *si)
{
    const fm1_health *h = health_get();
    si->uptime_ms = timer_get_ms();
    si->free_heap = get_malloc_remain_heap_size();
    si->stack_free_bridge = fm1_log_stack_free(FM1_TASK_BRIDGE);
    si->stack_free_ui = fm1_log_stack_free(FM1_TASK_UI);
    si->boots = h->boots; si->wdt_resets = h->wdt_resets; si->soft_resets = h->soft_resets;
    si->hangs = h->hangs; si->last_reason = h->last_reason;
    si->minimal = FM1_MINIMAL ? 1 : 0;
}

static int send_ports;
static void send_cb(void *ctx, const midi_msg *m)
{
    (void)ctx;
    if (send_ports & 1) bridge_send_to(MIDI_PORT_USB, m);
    if (send_ports & 2) bridge_send_to(MIDI_PORT_BLE, m);
}

void editor_port_send_midi(int port, const uint8_t *bytes, uint32_t n)
{
    midi_parser p;
    send_ports = port;
    midi_parser_init(&p, send_cb, NULL);
    midi_parser_feed(&p, bytes, n);
}
