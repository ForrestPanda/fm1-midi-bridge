/* SPDX-License-Identifier: MIT */
#include "bridge_sim.h"
#include "typedef.h"
#include "generic/circular_buf.h"
#include "os/os_api.h"
#include "device/device.h"
#include "bridge.h"
#include "settings.h"
#include "editor.h"
#include "health.h"
#include "fm1_log.h"
#include "update/fm1_update.h"
#include "usb/usb_midi.h"
#include "bt_ble/ble_port.h"
#include "ui/midilog.h"
#include "midi/router.h"
#include <stdarg.h>

bridge_sim sim;

/* ---- SDK circular buffer, same all-or-nothing contract as the library ---- */
void cbuf_init(cbuffer_t *c, void *buf, u32 size) { c->buf = buf; c->size = size; c->rd = c->wr = c->len = 0; }
u32 cbuf_write(cbuffer_t *c, void *buf, u32 len)
{
    u32 i;
    if (c->size - c->len < len) return 0;
    for (i = 0; i < len; i++) { c->buf[c->wr] = ((u8 *)buf)[i]; c->wr = (c->wr + 1) % c->size; }
    c->len += len;
    return len;
}
u32 cbuf_read(cbuffer_t *c, void *buf, u32 len)
{
    u32 i;
    if (c->len < len) return 0;
    for (i = 0; i < len; i++) { ((u8 *)buf)[i] = c->buf[c->rd]; c->rd = (c->rd + 1) % c->size; }
    c->len -= len;
    return len;
}
u32 cbuf_get_data_len(cbuffer_t *c) { return c->len; }
void cbuf_clear(cbuffer_t *c) { c->rd = c->wr = c->len = 0; }

/* ---- clock, task API ---- */
u32 timer_get_ms(void) { return sim.ms; }
int os_task_create(void (*func)(void *), void *parm, int prio, int stack_words, int qsize, const char *name)
{
    (void)func; (void)parm; (void)prio; (void)stack_words; (void)qsize; (void)name;
    sim.task_created++;
    return 0;
}
void os_time_dly(int ticks) { sim.ms += 10u * (u32)ticks; }
/* the bridge loop's wake semaphore: the tests drive bridge_step() themselves, so a pend
 * only passes the time it would have slept */
int  os_sem_create(OS_SEM *sem, int cnt) { sem->cnt = cnt; return 0; }
int  os_sem_post(OS_SEM *sem) { sem->cnt++; return 0; }
int  os_sem_pend(OS_SEM *sem, int timeout_ticks)
{
    if (sem->cnt > 0) { sem->cnt--; return 0; }
    sim.ms += 10u * (u32)timeout_ticks;
    return -1;
}

/* ---- UART1 ---- */
int trs_uart_init(void) { return sim.uart_fail ? -1 : 0; }   /* the register-level driver (trs_uart.c) is target-only */
void trs_uart_counts(u32 *rx_irq, u32 *ot_irq, u32 *bytes, u32 *breaks) { *rx_irq = *ot_irq = *bytes = *breaks = 0; }
u32 trs_uart_con(void) { return 0; }
static int uart_handle;
void *dev_open(const char *name, void *arg) { (void)name; (void)arg; return sim.uart_fail ? NULL : &uart_handle; }
int dev_ioctl(void *dev, u32 cmd, u32 arg) { (void)dev; (void)cmd; (void)arg; return 0; }
int dev_read(void *dev, void *buf, u32 len)
{
    u32 n = sim.trs_len - sim.trs_pos;
    (void)dev;
    if (n > len) n = len;
    memcpy(buf, sim.trs + sim.trs_pos, n);
    sim.trs_pos += n;
    if (sim.trs_pos == sim.trs_len) sim.trs_pos = sim.trs_len = 0;
    return (int)n;
}
void sim_trs_push(const uint8_t *bytes, size_t n)
{
    bridge_trs_rx(bytes, (u32)n);         /* what trs_task does on the target; dev_read above is unused now */
}

/* ---- USB MIDI ---- */
static void (*usb_rx)(void *, const u8 *, u32); static void *usb_rx_priv;
void usb_midi_set_rx_handler(const usb_dev id, void *priv, void (*rx)(void *priv, const u8 *pkts, u32 len)) { (void)id; usb_rx = rx; usb_rx_priv = priv; }
int usb_midi_configured(const usb_dev id) { (void)id; return sim.usb_configured; }
u32 usb_midi_write(const usb_dev id, const u8 *pkts, u32 len)
{
    (void)id;
    if (!sim.usb_configured) return 0;
    if (len > SIM_USB_OUT_MAX - sim.usb_out_len) { sim.usb_out_dropped += len; return len; }   /* capture full: count, keep going */
    memcpy(sim.usb_out + sim.usb_out_len, pkts, len);
    sim.usb_out_len += len;
    return len;
}
void sim_usb_push(const uint8_t *pkts, uint32_t n) { if (usb_rx) usb_rx(usb_rx_priv, pkts, n); }

/* ---- BLE port ---- */
static void (*ble_rx)(void *, const u8 *, u16); static void *ble_rx_priv;
void ble_port_set_rx(void *priv, void (*cb)(void *priv, const u8 *pkt, u16 len)) { ble_rx = cb; ble_rx_priv = priv; }
int ble_port_ready(void) { return sim.ble_ready; }
u16 ble_port_max_packet(void) { return sim.ble_max; }
int ble_port_send(const u8 *pkt, u16 len)
{
    int i = sim.ble_out_n;
    if (sim.ble_full) return 1;
    if (i >= SIM_BLE_OUT_MAX) { memmove(sim.ble_out[0], sim.ble_out[1], sizeof sim.ble_out - sizeof sim.ble_out[0]);
                                 memmove(sim.ble_out_len, sim.ble_out_len + 1, sizeof sim.ble_out_len - sizeof sim.ble_out_len[0]); i = SIM_BLE_OUT_MAX - 1; }
    if (len > BLEMIDI_MAX_PACKET) len = BLEMIDI_MAX_PACKET;
    memcpy(sim.ble_out[i], pkt, len); sim.ble_out_len[i] = len;
    sim.ble_out_n = i + 1;
    return 0;
}
void ble_port_set_mode(int mode) { (void)mode; }
void ble_port_set_link_mode(int mode) { (void)mode; }
void ble_port_boot(void) {}
const char *ble_port_state_text(void) { return "sim"; }
void sim_ble_push(const uint8_t *pkt, uint16_t len) { if (ble_rx) ble_rx(ble_rx_priv, pkt, len); }

/* ---- the screen logger, health, log ---- */
void midilog_init(void) {}
void midilog_add(int port, const midi_msg *m) { (void)port; (void)m; sim.midilog_in++; }
void midilog_add_out(int port, const midi_msg *m) { (void)port; (void)m; sim.midilog_out++; }
void health_bridge_alive(void) { sim.alive++; }
static logring ring;
void fm1_log(const char *fmt, ...) { (void)fmt; }
const logring *fm1_log_ring(void) { return &ring; }
void fm1_log_stack_note(int task) { (void)task; }
uint32_t fm1_log_stack_free(int task) { (void)task; return 0; }

/* ---- update entry: the bridge hands over whole frames ---- */
void fm1_update_port_frame(const u8 *p, u32 n)
{
    if (n > sizeof sim.upd_frame) n = sizeof sim.upd_frame;
    memcpy(sim.upd_frame, p, n); sim.upd_frame_len = n; sim.upd_frames++;
}
int fm1_update_poll(void) { sim.update_polls++; return 0; }
void fm1_update_boot_cleanup(void) { sim.boot_cleanups++; }

/* ---- editor port: what app/editor_port.c does, minus the UI ---- */
int editor_port_send(const uint8_t *wire, uint32_t n)
{
    if (n > sizeof sim.editor_reply) n = sizeof sim.editor_reply;
    memcpy(sim.editor_reply, wire, n); sim.editor_reply_len = n; sim.editor_replies++;
    return 0;
}
void editor_port_apply(void) { settings_apply_router(settings_get(), bridge_router()); }
void editor_port_status(editor_status *st)
{
    midi_router *r = bridge_router();
    int i;
    for (i = 0; i < 4; i++) { st->count_in[i] = r->count_in[i]; st->count_out[i] = r->count_out[i]; }
    st->dropped = r->count_dropped;
    st->usb_ok = (uint8_t)sim.usb_configured; st->ble_ok = (uint8_t)sim.ble_ready; st->ble_state = (uint8_t)sim.ble_ready;
}
const struct logring *editor_port_log(void) { return &ring; }
void editor_port_sysinfo(editor_sysinfo *si) { si->uptime_ms = sim.ms; }
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

/* ---- settings storage ---- */
static uint8_t blob[8][512]; static int blob_len[8];
static int slot_of(uint16_t id) { return id == FM1_SETTINGS_ID ? 0 : id >= 11 && id < 15 ? 1 + (id - 11) : 7; }
int settings_port_read(uint16_t id, void *buf, uint16_t len)
{
    int s = slot_of(id);
    if (!blob_len[s]) return -1;
    if (len > blob_len[s]) len = (uint16_t)blob_len[s];
    memcpy(buf, blob[s], len); return len;
}
int settings_port_write(uint16_t id, const void *buf, uint16_t len)
{
    int s = slot_of(id);
    if (len > sizeof blob[s]) len = sizeof blob[s];
    memcpy(blob[s], buf, len); blob_len[s] = len; return len;
}
uint32_t settings_port_now_ms(void) { return sim.ms; }

void sim_clear_out(void)
{
    sim.usb_out_len = 0; sim.usb_out_dropped = 0; sim.ble_out_n = 0;
    sim.editor_replies = 0; sim.editor_reply_len = 0; sim.upd_frames = 0; sim.upd_frame_len = 0;
}

void sim_reset(void)
{
    memset(&sim, 0, sizeof sim);
    memset(blob_len, 0, sizeof blob_len);
    sim.usb_configured = 1; sim.ble_ready = 1; sim.ble_max = 20;
    logring_init(&ring);
    usb_rx = NULL; ble_rx = NULL;
    settings_init();
}
