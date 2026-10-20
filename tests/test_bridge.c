/* Host test of the bridge task (app/bridge.c) on the simulator in tests/host:
 * ports in and out, the SysEx interception for the updater and the editor, the
 * inject ring, panic, link-drop panic, the clock master and the recorder. */
#include "bridge.h"
#include "settings.h"
#include "editor.h"
#include "update/fm1_update.h"
#include "midi/usbmidi.h"
#include "midi/blemidi.h"
#include "midi/recorder.h"
#include "bridge_sim.h"
#include <stdio.h>
#include <string.h>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

/* decode what reached USB and BLE back into messages */
static midi_msg got[4096]; static int ngot;
static void keep(void *ctx, const midi_msg *m) { (void)ctx; if (ngot < 4096) got[ngot++] = *m; }
static void keep_ble(void *ctx, uint16_t ts, const midi_msg *m) { (void)ctx; (void)ts; keep(NULL, m); }
static int usb_msgs(void)
{
    usbmidi_dec d; ngot = 0;
    usbmidi_dec_init(&d, keep, NULL);
    usbmidi_dec_feed(&d, sim.usb_out, sim.usb_out_len);
    return ngot;
}
static int ble_msgs(void)
{
    blemidi_dec d; int i; ngot = 0;
    blemidi_dec_init(&d, keep_ble, NULL);
    for (i = 0; i < sim.ble_out_n; i++) blemidi_dec_feed(&d, sim.ble_out[i], sim.ble_out_len[i]);
    return ngot;
}
static int count_status(uint8_t s)      /* channel statuses on any channel; system bytes exactly */
{
    int i, n = 0;
    for (i = 0; i < ngot; i++) if ((s < 0xF0 ? (got[i].b[0] & 0xF0) : got[i].b[0]) == s) n++;
    return n;
}

static void fresh(void) { sim_reset(); bridge_init(); sim_clear_out(); }
static void steps(int n) { while (n--) bridge_step(); }
static void steps_ms(int n_steps, uint32_t ms_each) { while (n_steps--) { bridge_step(); sim.ms += ms_each; } }
/* SysEx bytes F0..F7 as USB-MIDI packets, pushed in bursts that fit the 512-byte USB
 * ring, with a bridge step between bursts (a long dump arrives over many USB frames) */
static void usb_sysex(const uint8_t *s, size_t n)
{
    uint8_t pk[256]; size_t i = 0, w = 0;
    while (i < n) {
        size_t left = n - i;
        if (left > 3) { pk[w++] = 0x04; pk[w++] = s[i]; pk[w++] = s[i + 1]; pk[w++] = s[i + 2]; i += 3; }
        else { pk[w++] = (uint8_t)(0x04 + left); pk[w++] = s[i]; pk[w++] = left > 1 ? s[i + 1] : 0; pk[w++] = left > 2 ? s[i + 2] : 0; i = n; }
        if (w == sizeof pk) { sim_usb_push(pk, (uint32_t)w); w = 0; steps(4); }   /* 64 bytes per step */
    }
    if (w) sim_usb_push(pk, (uint32_t)w);
}
static midi_msg note(uint8_t st, uint8_t a, uint8_t b) { midi_msg m; m.kind = MIDI_KIND_CHANNEL; m.flags = 0; m.len = 3; m.b[0] = st; m.b[1] = a; m.b[2] = b; return m; }

static void test_boot(void)
{
    fresh();
    CHECK(sim.task_created == 1 && sim.boot_cleanups == 1);   /* the bridge task only: TRS bytes arrive from the UART1 interrupt (trs_uart.c), no reader task */
    CHECK(bridge_router()->routes == settings_get()->routes);
    CHECK(bridge_step() == 0 && sim.update_polls == 1);
    CHECK(sim.usb_out_len == 0 && sim.ble_out_n == 0);
}

static void test_trs(void)
{
    static const uint8_t on_off[] = { 0x90, 0x3C, 0x64, 0x80, 0x3C, 0x00 };
    static const uint8_t running[] = { 0x90, 0x40, 0x64, 0x40, 0x00 };
    fresh();
    sim_trs_push(on_off, sizeof on_off);
    steps(1);
    CHECK(usb_msgs() == 2 && got[0].b[0] == 0x90 && got[0].b[1] == 0x3C && got[1].b[0] == 0x80);
    CHECK(sim.ble_out_n == 1);                                   /* one packet per burst */
    CHECK(ble_msgs() == 2 && got[1].b[0] == 0x80 && got[1].b[1] == 0x3C);
    CHECK(bridge_router()->count_in[MIDI_PORT_TRS] == 2);
    CHECK(bridge_router()->count_out[MIDI_PORT_USB] == 2 && bridge_router()->count_out[MIDI_PORT_BLE] == 2);
    CHECK(sim.midilog_in == 2);
    sim_clear_out();
    sim_trs_push(running, sizeof running);                       /* running status */
    steps(1);
    CHECK(usb_msgs() == 2 && got[1].b[0] == 0x90 && got[1].b[2] == 0x00);
    /* the TRS bytes arrive in pieces: a message split across two reads still comes out once */
    sim_clear_out();
    sim_trs_push(on_off, 2); steps(1); CHECK(sim.usb_out_len == 0);
    sim_trs_push(on_off + 2, 4); steps(1); CHECK(usb_msgs() == 2);
}

static void test_usb_ble(void)
{
    static const uint8_t usb_note[] = { 0x09, 0x90, 0x40, 0x64 };
    static const uint8_t ble_note[] = { 0x80, 0x80, 0x90, 0x41, 0x64 };
    fresh();
    sim_usb_push(usb_note, 4);
    steps(1);
    CHECK(sim.usb_out_len == 0);                                 /* USB -> USB is not a route */
    CHECK(ble_msgs() == 1 && got[0].b[1] == 0x40);
    CHECK(bridge_router()->count_in[MIDI_PORT_USB] == 1);
    sim_clear_out();
    sim_ble_push(ble_note, sizeof ble_note);
    steps(1);
    CHECK(usb_msgs() == 1 && got[0].b[1] == 0x41);
    CHECK(sim.ble_out_n == 0);
    CHECK(bridge_router()->count_in[MIDI_PORT_BLE] == 1);
    /* a BLE packet larger than the protocol maximum is refused at the door */
    sim_clear_out();
    { static uint8_t big[600]; memset(big, 0x80, sizeof big); sim_ble_push(big, sizeof big); }
    steps(2);
    CHECK(sim.usb_out_len == 0 && bridge_router()->count_in[MIDI_PORT_BLE] == 1);
}

static void test_routes_and_filters(void)
{
    static const uint8_t on[] = { 0x90, 0x3C, 0x64 };
    static const uint8_t clock[] = { 0xF8 };
    fresh();
    settings_get()->routes = 0; editor_port_apply();
    sim_trs_push(on, 3); steps(1);
    CHECK(sim.usb_out_len == 0 && sim.ble_out_n == 0 && bridge_router()->count_in[MIDI_PORT_TRS] == 1);
    settings_get()->routes = ROUTE_TRS_USB; editor_port_apply();
    sim_trs_push(on, 3); steps(1);
    CHECK(usb_msgs() == 1 && sim.ble_out_n == 0);
    sim_clear_out();
    settings_get()->filters = FILTER_DROP_CLOCK; editor_port_apply();
    sim_trs_push(clock, 1); steps(1);
    CHECK(sim.usb_out_len == 0 && bridge_router()->count_dropped == 1);
    settings_get()->filters = 0; editor_port_apply();
    sim_trs_push(clock, 1); steps(1);
    CHECK(usb_msgs() == 1 && got[0].b[0] == 0xF8);
}

static void test_sysex_dispatch(void)
{
    static const uint8_t info[] = { 0xF0, 0x7D, 'F', 'M', '1', 0x01, 0xF7 };
    static const uint8_t hs[] = { 0xF0, 0x00, 0x32, 0x45, 0x00, 0x00, 0x00, 0x40, 0x7F, 0xF7 };
    static const uint8_t softkey[] = { 0xF0, 0x22, 0x24, 0x35, 0x7D, 0xF7 };
    static const uint8_t inquiry[] = { 0xF0, 0x7E, 0x7F, 0x06, 0x01, 0xF7 };
    static uint8_t huge[UPD_WIRE_MAX + 100];
    size_t i;
    fresh();
    /* editor frame: answered, not routed */
    usb_sysex(info, sizeof info); steps(1);
    CHECK(sim.editor_replies == 1 && sim.editor_reply_len > 6 && sim.editor_reply[5] == 0x41);
    CHECK(sim.ble_out_n == 0 && bridge_router()->count_in[MIDI_PORT_USB] == 0 && sim.upd_frames == 0);
    /* update handshake (pack7 prefix) and the raw soft key: handed over whole */
    usb_sysex(hs, sizeof hs); steps(1);
    CHECK(sim.upd_frames == 1 && sim.upd_frame_len == sizeof hs && memcmp(sim.upd_frame, hs, sizeof hs) == 0);
    usb_sysex(softkey, sizeof softkey); steps(1);
    CHECK(sim.upd_frames == 2 && sim.upd_frame_len == sizeof softkey && memcmp(sim.upd_frame, softkey, sizeof softkey) == 0);
    CHECK(sim.ble_out_n == 0 && bridge_router()->count_in[MIDI_PORT_USB] == 0);
    /* anybody else's SysEx is routed as usual */
    usb_sysex(inquiry, sizeof inquiry); steps(1);
    CHECK(sim.upd_frames == 2 && sim.editor_replies == 1);
    CHECK(ble_msgs() == 1 && got[0].kind == MIDI_KIND_SYSEX && got[0].len == 6 && got[0].b[1] == 0x7E);
    CHECK(bridge_router()->count_in[MIDI_PORT_USB] == 1);
    /* an "ours"-looking SysEx longer than any frame we accept is swallowed whole: no tail leaks out */
    sim_clear_out();
    huge[0] = 0xF0; huge[1] = 0x7D; huge[2] = 'F';
    for (i = 3; i < sizeof huge - 1; i++) huge[i] = 0x11;
    huge[sizeof huge - 1] = 0xF7;
    usb_sysex(huge, sizeof huge); steps(8);
    CHECK(sim.upd_frames == 0 && sim.editor_replies == 0);
    CHECK(sim.ble_out_n == 0 && bridge_router()->count_in[MIDI_PORT_USB] == 1);
    /* and the next ordinary message after it is fine */
    usb_sysex(inquiry, sizeof inquiry); steps(1);
    CHECK(ble_msgs() == 1 && got[0].len == 6);
}

static void test_ble_sysex_split(void)
{
    uint8_t sx[60]; size_t i;
    fresh();
    sim.ble_max = 20;
    sx[0] = 0xF0; for (i = 1; i < 59; i++) sx[i] = (uint8_t)(i & 0x7F); sx[59] = 0xF7;
    sim_trs_push(sx, sizeof sx); steps(4);
    CHECK(sim.ble_out_n >= 4);                                     /* 60 bytes do not fit 20-byte packets */
    for (i = 0; i < (size_t)sim.ble_out_n; i++) CHECK(sim.ble_out_len[i] <= 20);
    {
        /* reassemble: the chunks must concatenate to the original */
        uint8_t all[80]; size_t n = 0; int k;
        ble_msgs();
        for (k = 0; k < ngot; k++) { CHECK(got[k].kind == MIDI_KIND_SYSEX); if (n + got[k].len <= sizeof all) { memcpy(all + n, got[k].b, got[k].len); n += got[k].len; } }
        CHECK(n == sizeof sx && memcmp(all, sx, n) == 0);
    }
    CHECK(usb_msgs() >= 1 && got[0].b[0] == 0xF0);                 /* USB got it too */
}

static void test_inject(void)
{
    midi_msg m = note(0x90, 0x30, 0x50);
    int i;
    fresh();
    bridge_inject(MIDI_PORT_KBD, &m); steps(1);
    CHECK(usb_msgs() == 1 && got[0].b[1] == 0x30);
    CHECK(ble_msgs() == 1);
    CHECK(bridge_router()->count_in[MIDI_PORT_KBD] == 1);
    sim_clear_out();
    m = note(0x90, 0x31, 0x50);
    bridge_send_to(MIDI_PORT_USB, &m); steps(1);
    CHECK(usb_msgs() == 1 && sim.ble_out_n == 0 && sim.midilog_out == 1);
    CHECK(bridge_router()->count_out[MIDI_PORT_USB] == 2);
    /* the ring holds 32 records; the 33rd before a step is dropped whole, the rest come out intact */
    fresh();
    for (i = 0; i < 40; i++) { midi_msg k = note(0x90, (uint8_t)(0x20 + i), 0x40); bridge_send_to(MIDI_PORT_USB, &k); }
    steps(40);
    CHECK(usb_msgs() == 32);
    for (i = 0; i < ngot; i++) CHECK(got[i].b[0] == 0x90 && got[i].b[1] == 0x20 + i && got[i].b[2] == 0x40);
}

static void test_panic(void)
{
    int n = 0;
    fresh();
    sim.ble_max = 512;                                              /* big notifications: the capture keeps 64 packets */
    bridge_panic_request();
    CHECK(bridge_panic_active());
    while (bridge_panic_active() && n < 100) { bridge_step(); n++; }
    CHECK(!bridge_panic_active() && n < 100);
    CHECK(usb_msgs() == 2080);
    CHECK(count_status(0xB0) == 32 && count_status(0x80) == 2048);
    CHECK(ble_msgs() == 2080);
}

static void test_link_drop(void)
{
    fresh();
    steps(1);                                                       /* link_watch sees both up */
    sim.usb_configured = 0; steps(4);
    CHECK(sim.usb_out_len == 0);
    CHECK(ble_msgs() == 32);                                        /* short panic to the surviving output */
    CHECK(count_status(0xB0) == 32);
    sim_clear_out();
    sim.usb_configured = 1; steps(1);
    sim.ble_ready = 0; steps(4);
    CHECK(usb_msgs() == 32 && sim.ble_out_n == 0);
    sim_clear_out();
    sim.ble_ready = 1; steps(1);
    settings_get()->panic_on_disconnect = 0;
    sim.usb_configured = 0; steps(4);
    CHECK(sim.ble_out_n == 0);
    sim.usb_configured = 1; steps(1);
}

static void test_clock(void)
{
    fresh();
    settings_get()->clock_bpm = 120; settings_get()->clock_to = 1;
    bridge_clock_run(1);
    CHECK(bridge_clock_running());
    CHECK(usb_msgs() == 1 && got[0].b[0] == 0xFA);
    sim_clear_out();
    steps_ms(100, 10);                                              /* one second */
    usb_msgs();
    CHECK(ngot >= 47 && ngot <= 49 && count_status(0xF8) == ngot);        /* 24 per quarter at 120 bpm */
    CHECK(sim.ble_out_n == 0);
    sim_clear_out();
    bridge_clock_run(0);
    CHECK(!bridge_clock_running() && usb_msgs() == 1 && got[0].b[0] == 0xFC);
    /* a long stall does not burst: at most 8 per step, then the backlog is dropped */
    sim_clear_out();
    bridge_clock_run(1); sim_clear_out();
    sim.ms += 5000; steps(1);
    CHECK(usb_msgs() <= 8);
    bridge_clock_run(0);
}

static void test_recorder(void)
{
    static const uint8_t on[] = { 0x90, 0x3C, 0x64 }, off[] = { 0x80, 0x3C, 0x00 };
    midi_recorder *r = bridge_recorder();
    fresh();
    bridge_recorder_ports(1);
    rec_start(r, sim.ms);
    sim_trs_push(on, 3); steps(1); sim.ms += 100;
    sim_trs_push(off, 3); steps(1); sim.ms += 100;
    rec_stop(r, sim.ms);
    CHECK(r->count == 2 && rec_duration(r) == 200);
    sim_clear_out();
    rec_play(r, sim.ms, 0);
    steps_ms(30, 10);
    CHECK(usb_msgs() == 2 && got[0].b[0] == 0x90 && got[1].b[0] == 0x80);
    CHECK(sim.ble_out_n == 0);
}

static void test_merge_guard(void)
{
    static const uint8_t twice[] = { 0x90, 0x3C, 0x64, 0x90, 0x3C, 0x64 };
    fresh();
    sim_trs_push(twice, sizeof twice); steps(1);
    CHECK(usb_msgs() == 3 && got[1].b[0] == 0x80 && got[2].b[0] == 0x90);   /* retrigger: off between the two ons */
    sim_clear_out(); fresh();
    settings_get()->merge_guard = 0;
    sim_trs_push(twice, sizeof twice); steps(1);
    CHECK(usb_msgs() == 2);
}

/* a BLE stack that refuses sends: the packet waits and the next messages batch behind it; a
 * link that stays refused for 250 ms costs that one packet, counted */
static void test_ble_busy(void)
{
    static const uint8_t n1[] = { 0x90, 0x3C, 0x64 }, n2[] = { 0x90, 0x3E, 0x64 }, n3[] = { 0x90, 0x40, 0x64 };
    fresh();
    sim.ble_full = 1;
    sim_trs_push(n1, 3); steps_ms(1, 10);
    sim_trs_push(n2, 3); steps_ms(1, 10);
    sim_trs_push(n3, 3); steps_ms(1, 10);
    CHECK(sim.ble_out_n == 0 && bridge_ble_drops() == 0);             /* nothing sent, nothing lost */
    sim.ble_full = 0;
    steps_ms(2, 10);
    CHECK(sim.ble_out_n == 2 && ble_msgs() == 3);                     /* the waiting packet, then the two batched behind it */
    sim_clear_out();
    sim.ble_full = 1;
    sim_trs_push(n1, 3); steps_ms(30, 10);                            /* 300 ms refused: given up, counted */
    CHECK(sim.ble_out_n == 0 && bridge_ble_drops() == 1);
    sim.ble_full = 0;
}

/* a burst of several packets behind a stack that is busy for 50 ms: every packet waits its
 * turn in the queue, nothing lost (before the queue, all but the last two packets went) */
static void test_ble_burst(void)
{
    uint8_t burst[90]; int i; uint32_t drops0;
    fresh();
    drops0 = bridge_ble_drops();
    for (i = 0; i < 30; i++) { burst[3 * i] = 0x90; burst[3 * i + 1] = (uint8_t)(40 + i); burst[3 * i + 2] = 0x64; }
    sim.ble_full = 1;
    sim_trs_push(burst, sizeof burst); steps_ms(5, 10);
    CHECK(sim.ble_out_n == 0);
    sim.ble_full = 0;
    steps_ms(10, 10);
    CHECK(sim.ble_out_n >= 4 && ble_msgs() == 30 && bridge_ble_drops() == drops0);
    for (i = 0; i < 30; i++) CHECK(got[i].b[1] == 40 + i);           /* in order */
}

/* real time inside a SysEx from TRS: the SysEx reaches USB whole, the real time around it */
static void test_usb_sysex_realtime(void)
{
    static const uint8_t in[] = { 0xF0, 0x7D, 0x01, 0x02, 0xF8, 0x03, 0x04, 0xFA, 0x05, 0xF7 };
    static const uint8_t want[] = { 0xF0, 0x7D, 0x01, 0x02, 0x03, 0x04, 0x05, 0xF7 };
    uint8_t sx[16]; int i, k, n = 0, rt = 0;
    fresh();
    for (k = 0; k < 3; k++) {                                        /* again: no carry left behind */
        sim_clear_out();
        sim_trs_push(in, sizeof in); steps(2);
        usb_msgs();
        n = 0; rt = 0;
        for (i = 0; i < ngot; i++) {
            if (got[i].kind == MIDI_KIND_SYSEX) { memcpy(sx + n, got[i].b, got[i].len); n += got[i].len; }
            else if (got[i].kind == MIDI_KIND_REALTIME) rt++;
        }
        CHECK(n == (int)sizeof want && memcmp(sx, want, sizeof want) == 0 && rt == 2);
    }
}

static void test_usb_ring_full(void)
{
    static uint8_t burst[1024]; int i;
    fresh();
    for (i = 0; i < 256; i++) { burst[i * 4] = 0x09; burst[i * 4 + 1] = 0x90; burst[i * 4 + 2] = 0x40; burst[i * 4 + 3] = 0x40; }
    sim_usb_push(burst, 1024);                                      /* the ring holds 2048: one update reply (808) and more */
    sim_usb_push(burst, 1024);                                      /* full */
    sim_usb_push(burst, 1024);                                      /* no room: refused whole */
    steps(40);
    CHECK(bridge_router()->count_in[MIDI_PORT_USB] == 512);
    CHECK(ble_msgs() > 0);
}

static void test_no_uart(void)
{
    static const uint8_t usb_note[] = { 0x09, 0x90, 0x40, 0x64 };
    sim_reset(); sim.uart_fail = 1; bridge_init(); sim_clear_out();
    CHECK(bridge_step() == 0);
    sim_usb_push(usb_note, 4); steps(1);
    CHECK(ble_msgs() == 1);                                         /* the other ports still work */
}

int main(void)
{
    test_boot();
    test_trs();
    test_usb_ble();
    test_routes_and_filters();
    test_sysex_dispatch();
    test_ble_sysex_split();
    test_inject();
    test_panic();
    test_link_drop();
    test_clock();
    test_recorder();
    test_merge_guard();
    test_usb_ring_full();
    test_ble_busy();
    test_ble_burst();
    test_usb_sysex_realtime();
    test_no_uart();
    if (fails) { printf("%d failure(s)\n", fails); return 1; }
    printf("bridge tests passed\n");
    return 0;
}
