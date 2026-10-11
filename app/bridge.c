/* SPDX-License-Identifier: MIT */
/* Bridge task: pulls bytes from the three ports, parses them into MIDI messages,
 * fans them out through the router and re-encodes for each destination.
 *
 *   TRS  UART1 RX ring (SDK uart device "uart1")  -> midi_parser  -> router
 *   USB  bulk OUT packets (ISR, usb_midi.c)       -> cbuf -> usbmidi_dec -> router
 *   BLE  writes to the MIDI characteristic (BT task) -> cbuf -> blemidi_dec -> router
 *   router -> USB: usbmidi_encode -> usb_midi_write
 *   router -> BLE: blemidi_enc (one packet per burst) -> ble_midi_send
 */
#include "system/includes.h"
#include "app_config.h"
#include "os/os_api.h"
#include "generic/circular_buf.h"
#include "device/uart.h"
#include "device/device.h"
#include "bridge.h"
#include "midi/midi_msg.h"
#include "midi/usbmidi.h"
#include "midi/blemidi.h"
#include "usb/usb_midi.h"
#include "bt_ble/ble_port.h"
#include "update/fm1_update.h"
#include "ui/midilog.h"
#include "settings.h"
#include "midi/panic.h"

#define LOG_TAG_CONST       APP
#define LOG_TAG             "[BRIDGE]"
#define LOG_ERROR_ENABLE
#define LOG_INFO_ENABLE
#include "debug.h"

#define UART_RING   256
#define USB_RING    512
#define BLE_RING    1024

static midi_router router;
static midi_parser trs_parser;
static usbmidi_dec usb_dec;
static blemidi_dec ble_dec;
static blemidi_enc ble_enc;

static void *uart_dev;
static u8 uart_ring[UART_RING] __attribute__((aligned(4)));
static cbuffer_t usb_cbuf, ble_cbuf;
static u8 usb_ring[USB_RING], ble_ring[BLE_RING];
static u8 ble_pending;                  /* something was added to ble_enc since the last flush */
static midi_panic panic;                /* midi_panic_active() while a panic is being sent */
static volatile u8 panic_req;

/* ------------------------------------------------------------------ sinks */

static void sink_usb(void *ctx, const midi_msg *m)
{
    u8 out[(MIDI_SYSEX_CHUNK / 3 + 1) * 4];
    u16 n = usbmidi_encode(0, m, out);
    (void)ctx;
    if (n && usb_midi_configured(0)) {
        usb_midi_write(0, out, n);
    }
}

static void ble_flush(void)
{
    u16 len;
    const u8 *pkt;
    if (blemidi_enc_empty(&ble_enc)) return;
    pkt = blemidi_enc_take(&ble_enc, &len);
    ble_pending = 0;
    if (ble_port_ready()) {
        ble_port_send(pkt, len);        /* dropped if the stack's buffer is full */
    }
}

static void ble_add(uint32_t ts, const midi_msg *m)
{
    int r = blemidi_enc_add(&ble_enc, ts, m);
    if (r == 1) { ble_pending = 1; return; }
    ble_flush();
    r = blemidi_enc_add(&ble_enc, ts, m);
    if (r == 1) ble_pending = 1;
    /* r == -1: a SysEx piece larger than a whole packet; sink_ble splits so this cannot happen */
}

static void sink_ble(void *ctx, const midi_msg *m)
{
    uint32_t ts = timer_get_ms();
    (void)ctx;
    if (!ble_port_ready()) return;
    blemidi_enc_set_max(&ble_enc, ble_port_max_packet());
    if (m->kind != MIDI_KIND_SYSEX) {
        ble_add(ts, m);
        return;
    }
    {
        /* split a chunk into pieces that fit a packet (header + ts per high byte) */
        u16 max = ble_enc.max > 8 ? ble_enc.max - 4 : 4;   /* room for header, 2 timestamps */
        u8 off = 0;
        while (off < m->len) {
            midi_msg part = *m;
            u8 take = (u8)(m->len - off);
            if (take > max) take = (u8)max;
            part.len = take;
            memcpy(part.b, m->b + off, take);
            part.flags = (u8)(((off == 0) ? (m->flags & MIDI_SYSEX_START) : 0) |
                              ((off + take == m->len) ? (m->flags & MIDI_SYSEX_END) : 0));
            ble_add(ts, &part);
            off = (u8)(off + take);
        }
    }
}

void bridge_panic_request(void) { panic_req = 1; }
int  bridge_panic_active(void) { return panic_req || midi_panic_active(&panic); }

/* a few panic messages per loop, straight to the output ports (no routes, no filters) */
static int panic_step(void)
{
    midi_msg m;
    int n = 0;
    if (panic_req) { panic_req = 0; midi_panic_start(&panic); }
    while (n < 64 && midi_panic_next(&panic, &m)) {
        sink_usb(NULL, &m);
        sink_ble(NULL, &m);
        n++;
    }
    return n > 0;
}

/* ---------------------------------------------------------------- sources */

static void from_trs(void *ctx, const midi_msg *m) { (void)ctx; router_feed(&router, MIDI_PORT_TRS, m); }
/* SysEx from the host that belongs to the update protocol (wire prefix 00 32 =
 * pack7 of 00 59, or the raw 22 24 35 commands) is reassembled for fm1_update
 * instead of being routed. Everything else is routed. */
static u8 upd_acc[UPD_WIRE_MAX];
static u32 upd_acc_len;
static u8 upd_capturing;
extern void fm1_update_port_frame(const u8 *p, u32 n);

static void from_usb(void *ctx, const midi_msg *m)
{
    (void)ctx;
    if (m->kind == MIDI_KIND_SYSEX) {
        if (m->flags & MIDI_SYSEX_START) {
            upd_capturing = m->len >= 3 && ((m->b[1] == 0x00 && m->b[2] == 0x32) ||
                                            (m->b[1] == 0x22 && m->b[2] == 0x24));
            upd_acc_len = 0;
        }
        if (upd_capturing) {
            if (upd_acc_len + m->len <= sizeof upd_acc) {
                memcpy(upd_acc + upd_acc_len, m->b, m->len);
                upd_acc_len += m->len;
            } else {
                upd_capturing = 0;                 /* too long: not ours after all */
            }
            if (m->flags & MIDI_SYSEX_END) {
                if (upd_capturing) fm1_update_port_frame(upd_acc, upd_acc_len);
                upd_capturing = 0;
            }
            return;
        }
    }
    router_feed(&router, MIDI_PORT_USB, m);
}
static void from_ble(void *ctx, uint16_t ts, const midi_msg *m) { (void)ctx; (void)ts; router_feed(&router, MIDI_PORT_BLE, m); }

/* USB interrupt context: just queue */
static void usb_rx_isr(void *priv, const u8 *pkts, u32 len)
{
    (void)priv;
    cbuf_write(&usb_cbuf, (void *)pkts, len);
}

/* BT stack task: queue one delimited packet */
static void ble_rx_cb(void *priv, const u8 *pkt, u16 len)
{
    u8 tmp[2 + BLEMIDI_MAX_PACKET];
    (void)priv;
    if (len > BLEMIDI_MAX_PACKET) return;
    tmp[0] = (u8)len;
    tmp[1] = (u8)(len >> 8);
    memcpy(tmp + 2, pkt, len);
    cbuf_write(&ble_cbuf, tmp, 2u + len);
}

int bridge_pump_usb(void);
static int drain_usb(void)
{
    u8 buf[64];
    u32 n = cbuf_get_data_len(&usb_cbuf) & ~3u;
    if (!n) return 0;
    if (n > sizeof buf) n = sizeof buf;
    n = cbuf_read(&usb_cbuf, buf, n);
    usbmidi_dec_feed(&usb_dec, buf, n);
    return 1;
}

int bridge_pump_usb(void) { return drain_usb(); }

static int drain_ble(void)
{
    u8 hdr[2], pkt[BLEMIDI_MAX_PACKET];
    u16 len;
    if (cbuf_get_data_len(&ble_cbuf) < 2) return 0;
    cbuf_read(&ble_cbuf, hdr, 2);
    len = (u16)(hdr[0] | hdr[1] << 8);
    if (len > sizeof pkt || cbuf_get_data_len(&ble_cbuf) < len) {   /* corrupt: resync */
        cbuf_clear(&ble_cbuf);
        return 0;
    }
    cbuf_read(&ble_cbuf, pkt, len);
    blemidi_dec_feed(&ble_dec, pkt, len);
    return 1;
}

static int drain_trs(void)
{
    u8 buf[64];
    int n;
    if (!uart_dev) return 0;
    n = dev_read(uart_dev, buf, sizeof buf);          /* blocks up to the receive timeout */
    if (n <= 0) return 0;
    midi_parser_feed(&trs_parser, buf, (size_t)n);
    return 1;
}

static void log_tap(void *ctx, int src, const midi_msg *m)
{
    (void)ctx;
    midilog_add(src, m);
}

static void bridge_task(void *p)
{
    (void)p;
    midilog_init();
    for (;;) {
        int busy = 0;
        busy |= drain_trs();
        busy |= drain_usb();
        busy |= drain_ble();
        busy |= panic_step();
        if (ble_pending) ble_flush();                  /* one notification per burst */
        fm1_update_poll();                             /* handshake replies; a session blocks here */
        if (!busy && !uart_dev) os_time_dly(1);
    }
}

/* ------------------------------------------------------------------- init */

static void uart_init_trs(void)
{
    uart_dev = dev_open("uart1", NULL);
    if (!uart_dev) {
        log_error("uart1 (TRS MIDI IN) open failed");
        return;
    }
    dev_ioctl(uart_dev, UART_SET_CIRCULAR_BUFF_ADDR, (u32)uart_ring);
    dev_ioctl(uart_dev, UART_SET_CIRCULAR_BUFF_LENTH, (u32)sizeof uart_ring);
    dev_ioctl(uart_dev, UART_SET_RECV_BLOCK, 1);
    dev_ioctl(uart_dev, UART_SET_RECV_TIMEOUT, 2);   /* ms: paces the loop */
    dev_ioctl(uart_dev, UART_START, 0);
}

midi_router *bridge_router(void)
{
    return &router;
}

void bridge_init(void)
{
    router_init(&router, settings_get()->routes);
    router.filters = settings_get()->filters;
    router_set_sink(&router, MIDI_PORT_USB, sink_usb, NULL);
    router_set_sink(&router, MIDI_PORT_BLE, sink_ble, NULL);
    router_set_tap(&router, log_tap, NULL);
    midi_parser_init(&trs_parser, from_trs, NULL);
    usbmidi_dec_init(&usb_dec, from_usb, NULL);
    blemidi_dec_init(&ble_dec, from_ble, NULL);
    blemidi_enc_init(&ble_enc, 20);
    cbuf_init(&usb_cbuf, usb_ring, sizeof usb_ring);
    cbuf_init(&ble_cbuf, ble_ring, sizeof ble_ring);
    usb_midi_set_rx_handler(0, NULL, usb_rx_isr);
    ble_port_set_rx(NULL, ble_rx_cb);
    uart_init_trs();
    fm1_update_boot_cleanup();
    os_task_create(bridge_task, NULL, 10, 1024, 0, "midi_bridge");
    log_info("bridge up: routes 0x%x", router.routes);
}
