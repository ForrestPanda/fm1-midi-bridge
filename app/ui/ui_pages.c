/* SPDX-License-Identifier: MIT */
/* The pages. Layout: 30 columns x 17 rows (8x14 font on 240x240). */
#include "system/includes.h"
#include "app_config.h"
#include "os/os_api.h"
#include "asm/cpu.h"
#include "asm/adc_api.h"
#include "asm/gpio.h"
#include "ui.h"
#include "ui_menu.h"
#include "lcd.h"
#include "midilog.h"
#include "settings.h"
#include "bridge.h"
#include "midi/router.h"
#include "midi/keys.h"
#include "midi/compose.h"
#include "midi/decode.h"
#include "usb/usb_midi.h"
#include "bt_ble/ble_midi.h"
#include "bt_ble/ble_port.h"
#include "bt_ble/ble_host.h"
#include "update/fm1_update.h"
#include "health.h"
#include <stdio.h>

extern const ui_page ui_page_confirm;
extern void upd_port_uboot(void);
extern u16 bt_ble_is_connected(void);
extern void bt_ble_adv_enable(u8 enable);
extern void ble_app_disconnect(void);
extern bool ble_list_clear_all(void);
extern u16 ble_list_get_count(void);
extern void usb_port_apply(void);

static void mark(void) { settings_mark_dirty(); }
static void onoff(char *b, int n, int v) { snprintf(b, n, "%s", v ? "on" : "off"); }

/* ------------------------------------------------------------------ splash */
static u32 splash_ms;
static void splash_enter(void) { splash_ms = timer_get_ms(); }
static void splash_draw(void)
{
    char l[UI_COLS + 1];
    ui_row(5, "        FM-1 MIDI bridge", UI_C_TXT, UI_C_BG);
    snprintf(l, sizeof l, "            v%s", FM1_FW_VERSION);
    ui_row(7, l, UI_C_DIM, UI_C_BG);
    ui_row(10, "   TRS / USB / BLE  <->  MIDI", UI_C_DIM, UI_C_BG);
    snprintf(l, sizeof l, "     USB:%s   BLE:%s", usb_midi_configured(0) ? "ok" : "--", ble_midi_ready() ? "ok" : "--");
    ui_row(13, l, UI_C_OK, UI_C_BG);
}
static void splash_tick(void) { if (timer_get_ms() - splash_ms >= 1500) ui_home(); }
static void splash_event(const mx_event *e) { if (e->type == MX_EV_BTN_UP) ui_home(); }
static const ui_page page_splash = { "Splash", splash_enter, splash_draw, splash_event, splash_tick };

/* ------------------------------------------------------------------ main menu */
static void go_routes(void)  { ui_goto(PAGE_ROUTES); }
static void go_log(void)     { ui_goto(PAGE_LOG); }
static void go_ble(void)     { ui_goto(PAGE_BLE); }
static void go_usb(void)     { ui_goto(PAGE_USB); }
static void go_filters(void) { ui_goto(PAGE_FILTERS); }
static void go_keyboard(void) { ui_goto(PAGE_KEYBOARD); }
static void go_composer(void) { ui_goto(PAGE_COMPOSER); }
static void go_connections(void) { ui_goto(PAGE_CONNECTIONS); }
static void go_stats(void) { ui_goto(PAGE_STATS); }
static void go_recorder(void) { ui_goto(PAGE_RECORDER); }
static void go_reference(void) { ui_goto(PAGE_REFERENCE); }
static void go_clock(void) { ui_goto(PAGE_CLOCK); }
static void go_buttons(void) { ui_goto(PAGE_BUTTONS); }
static void go_display(void) { ui_goto(PAGE_DISPLAY); }
static void go_system(void)  { ui_goto(PAGE_SYSTEM); }
static const ui_item main_items[] = {
    { "Connections",   UI_SUBMENU, NULL, NULL, go_connections, NULL },
    { "Routing",       UI_SUBMENU, NULL, NULL, go_routes,  NULL },
    { "MIDI log",      UI_SUBMENU, NULL, NULL, go_log,     NULL },
    { "Composer",      UI_SUBMENU, NULL, NULL, go_composer, NULL },
    { "Statistics",    UI_SUBMENU, NULL, NULL, go_stats, NULL },
    { "Recorder",      UI_SUBMENU, NULL, NULL, go_recorder, NULL },
    { "MIDI reference", UI_SUBMENU, NULL, NULL, go_reference, NULL },
    { "Clock master",  UI_SUBMENU, NULL, NULL, go_clock, NULL },
    { "Keyboard",      UI_SUBMENU, NULL, NULL, go_keyboard, NULL },
    { "Button MIDI",   UI_SUBMENU, NULL, NULL, go_buttons, NULL },
    { "Bluetooth",     UI_SUBMENU, NULL, NULL, go_ble,     NULL },
    { "USB",           UI_SUBMENU, NULL, NULL, go_usb,     NULL },
    { "MIDI filters",  UI_SUBMENU, NULL, NULL, go_filters, NULL },
    { "Display",       UI_SUBMENU, NULL, NULL, go_display, NULL },
    { "System",        UI_SUBMENU, NULL, NULL, go_system,  NULL },
};
static ui_menu main_menu = { main_items, 15, 0, 0 };
static u16 bat_mv;
static void main_draw(void)
{
    char l[UI_COLS + 1];
    ui_menu_draw(&main_menu, "FM-1 MIDI bridge");
    snprintf(l, sizeof l, "USB:%s BLE:%s%s BAT:%u.%02uV", usb_midi_configured(0) ? "ok" : "--",
             ble_port_ready() ? "ok" : bt_ble_is_connected() ? "con" : "adv",
             settings_get()->ble_link_mode == BLE_LINK_POWER_SAVING ? " ECO" : "", bat_mv / 1000, (bat_mv % 1000) / 10);
    ui_row(UI_ROWS - 1, l, UI_C_DIM, UI_C_BG);
}
static void main_event(const mx_event *e) { ui_menu_event(&main_menu, e); }
static void main_tick(void)
{
    static u32 last;
    u32 now = timer_get_ms();
    if (now - last >= 1000) { last = now; bat_mv = (u16)adc_get_voltage(AD_CH_PB01); ui_invalidate(); }
}
static const ui_page page_main = { "FM-1 MIDI bridge", NULL, main_draw, main_event, main_tick };

/* ------------------------------------------------------------------ routes */
static void route_val(char *b, int n, int src, int dst) { onoff(b, n, router_enabled(bridge_router(), src, dst)); }
static void route_adj(int src, int dst)
{
    midi_router *r = bridge_router();
    router_set(r, src, dst, !router_enabled(r, src, dst));
    settings_get()->routes = r->routes;
    mark();
}
static void v_tb(char *b, int n) { route_val(b, n, MIDI_PORT_TRS, MIDI_PORT_BLE); }
static void v_tu(char *b, int n) { route_val(b, n, MIDI_PORT_TRS, MIDI_PORT_USB); }
static void v_ub(char *b, int n) { route_val(b, n, MIDI_PORT_USB, MIDI_PORT_BLE); }
static void v_bu(char *b, int n) { route_val(b, n, MIDI_PORT_BLE, MIDI_PORT_USB); }
static void v_ku(char *b, int n) { route_val(b, n, MIDI_PORT_KBD, MIDI_PORT_USB); }
static void v_kb(char *b, int n) { route_val(b, n, MIDI_PORT_KBD, MIDI_PORT_BLE); }
static void a_ku(int d) { (void)d; route_adj(MIDI_PORT_KBD, MIDI_PORT_USB); }
static void a_kb(int d) { (void)d; route_adj(MIDI_PORT_KBD, MIDI_PORT_BLE); }
static void a_tb(int d) { (void)d; route_adj(MIDI_PORT_TRS, MIDI_PORT_BLE); }
static void a_tu(int d) { (void)d; route_adj(MIDI_PORT_TRS, MIDI_PORT_USB); }
static void a_ub(int d) { (void)d; route_adj(MIDI_PORT_USB, MIDI_PORT_BLE); }
static void a_bu(int d) { (void)d; route_adj(MIDI_PORT_BLE, MIDI_PORT_USB); }
static void v_cnt(char *b, int n, int port) { midi_router *r = bridge_router(); snprintf(b, n, "%u/%u", (unsigned)r->count_in[port], (unsigned)r->count_out[port]); }
static void v_cnt_trs(char *b, int n) { v_cnt(b, n, MIDI_PORT_TRS); }
static void v_cnt_usb(char *b, int n) { v_cnt(b, n, MIDI_PORT_USB); }
static void v_cnt_ble(char *b, int n) { v_cnt(b, n, MIDI_PORT_BLE); }
/* per-route options: which route the page edits */
static int opt_src, opt_dst;
static void go_opt(int src, int dst) { opt_src = src; opt_dst = dst; ui_goto(PAGE_ROUTE_OPT); }
static void o_tb(void) { go_opt(MIDI_PORT_TRS, MIDI_PORT_BLE); }
static void o_tu(void) { go_opt(MIDI_PORT_TRS, MIDI_PORT_USB); }
static void o_ub(void) { go_opt(MIDI_PORT_USB, MIDI_PORT_BLE); }
static void o_bu(void) { go_opt(MIDI_PORT_BLE, MIDI_PORT_USB); }
static void o_ku(void) { go_opt(MIDI_PORT_KBD, MIDI_PORT_USB); }
static void o_kb(void) { go_opt(MIDI_PORT_KBD, MIDI_PORT_BLE); }
static void go_inputs(void) { ui_goto(PAGE_INPUTS); }
static const ui_item routes_items[] = {
    { "TRS in  -> BLE out", UI_TOGGLE, v_tb, a_tb, NULL, NULL },
    { "TRS in  -> USB out", UI_TOGGLE, v_tu, a_tu, NULL, NULL },
    { "USB in  -> BLE out", UI_TOGGLE, v_ub, a_ub, NULL, NULL },
    { "BLE in  -> USB out", UI_TOGGLE, v_bu, a_bu, NULL, NULL },
    { "Keys    -> USB out", UI_TOGGLE, v_ku, a_ku, NULL, NULL },
    { "Keys    -> BLE out", UI_TOGGLE, v_kb, a_kb, NULL, NULL },
    { "",                   UI_INFO,   NULL, NULL, NULL, NULL },
    { "Options TRS->BLE",   UI_SUBMENU, NULL, NULL, o_tb, NULL },
    { "Options TRS->USB",   UI_SUBMENU, NULL, NULL, o_tu, NULL },
    { "Options USB->BLE",   UI_SUBMENU, NULL, NULL, o_ub, NULL },
    { "Options BLE->USB",   UI_SUBMENU, NULL, NULL, o_bu, NULL },
    { "Options Keys->USB",  UI_SUBMENU, NULL, NULL, o_ku, NULL },
    { "Options Keys->BLE",  UI_SUBMENU, NULL, NULL, o_kb, NULL },
    { "Inputs",             UI_SUBMENU, NULL, NULL, go_inputs, NULL },
    { "",                   UI_INFO,   NULL, NULL, NULL, NULL },
    { "TRS in/out",         UI_INFO,   v_cnt_trs, NULL, NULL, NULL },
    { "USB in/out",         UI_INFO,   v_cnt_usb, NULL, NULL, NULL },
    { "BLE in/out",         UI_INFO,   v_cnt_ble, NULL, NULL, NULL },
};
static ui_menu routes_menu = { routes_items, 18, 0, 0 };
static void routes_enter(void) { ui_menu_reset(&routes_menu); }
static void routes_draw(void) { ui_menu_draw(&routes_menu, "Routing"); }
static void routes_event(const mx_event *e) { ui_menu_event(&routes_menu, e); }
static void routes_tick(void) { static u8 k; if (++k >= 5) { k = 0; ui_invalidate(); } }
static const ui_page page_routes = { "Routing", routes_enter, routes_draw, routes_event, routes_tick };

/* ------------------------------------------------------------------ route options */
static route_opt *cur_opt(void) { return router_opt(bridge_router(), opt_src, opt_dst); }
static void opt_store(void)
{
    fm1_settings *st = settings_get();
    route_opt *o = cur_opt();
    int i = ROUTE_INDEX(opt_src, opt_dst);
    st->route[i].channels = o->channels; st->route[i].remap = o->remap; st->route[i].flags = o->flags;
    mark();
}
/* channel filter as "all" or a single channel; the web editor can set any mask */
static int single_channel(uint16_t mask) { int c; for (c = 0; c < 16; c++) if (mask == (1u << c)) return c + 1; return 0; }
static void v_och(char *b, int n) { uint16_t m = cur_opt()->channels; int c = single_channel(m); if (m == 0xFFFF) snprintf(b, n, "all"); else if (c) snprintf(b, n, "%d only", c); else snprintf(b, n, "custom"); }
static void a_och(int d)
{
    route_opt *o = cur_opt(); int c = o->channels == 0xFFFF ? 0 : single_channel(o->channels);
    c += d > 0 ? 1 : -1; if (c < 0) c = 16; if (c > 16) c = 0;
    o->channels = c ? (uint16_t)(1u << (c - 1)) : 0xFFFF; opt_store();
}
static void v_oremap(char *b, int n) { if (cur_opt()->remap) snprintf(b, n, "to %d", cur_opt()->remap); else snprintf(b, n, "keep"); }
static void a_oremap(int d) { route_opt *o = cur_opt(); int r = o->remap + (d > 0 ? 1 : -1); if (r < 0) r = 16; if (r > 16) r = 0; o->remap = (u8)r; opt_store(); }
static void v_oclk(char *b, int n) { onoff(b, n, cur_opt()->flags & ROUTE_OPT_CLOCK); }
static void a_oclk(int d) { (void)d; cur_opt()->flags ^= ROUTE_OPT_CLOCK; opt_store(); }
static void v_osx(char *b, int n) { onoff(b, n, cur_opt()->flags & ROUTE_OPT_SYSEX); }
static void a_osx(int d) { (void)d; cur_opt()->flags ^= ROUTE_OPT_SYSEX; opt_store(); }
static const ui_item opt_items[] = {
    { "Channels",         UI_ENUM,   v_och,    a_och,    NULL, NULL },
    { "Remap channel",    UI_ENUM,   v_oremap, a_oremap, NULL, NULL },
    { "Clock / transport",UI_TOGGLE, v_oclk,   a_oclk,   NULL, NULL },
    { "SysEx",            UI_TOGGLE, v_osx,    a_osx,    NULL, NULL },
};
static ui_menu opt_menu = { opt_items, 4, 0, 0 };
static char opt_title[UI_COLS + 1];
static void opt_enter(void) { ui_menu_reset(&opt_menu); snprintf(opt_title, sizeof opt_title, "%s -> %s", router_port_name(opt_src), router_port_name(opt_dst)); }
static void opt_draw(void) { ui_menu_draw(&opt_menu, opt_title); }
static void opt_event(const mx_event *e) { ui_menu_event(&opt_menu, e); }
static const ui_page page_route_opt = { "Route", opt_enter, opt_draw, opt_event, NULL };

/* ------------------------------------------------------------------ inputs (TRS transform) */
static const char *const vel_names[VEL_CURVE_COUNT] = { "linear", "soft", "hard", "fixed" };
static void in_store(void) { settings_apply_router(settings_get(), bridge_router()); mark(); }
/* No "%+d" anywhere on the device: the SDK's printf does not know the '+' flag and
 * prints a bare "d" (seen in fm1-emu on this very row). The sign is spelt out. */
static const char *plus(int v) { return v < 0 ? "" : "+"; }
static void v_itr(char *b, int n) { int t = settings_get()->input[MIDI_PORT_TRS].transpose; snprintf(b, n, "%s%d", plus(t), t); }
static void a_itr(int d) { int t = settings_get()->input[MIDI_PORT_TRS].transpose + (d > 0 ? 1 : -1); if (t < -24) t = -24; if (t > 24) t = 24; settings_get()->input[MIDI_PORT_TRS].transpose = (s8)t; in_store(); }
static void v_ivc(char *b, int n) { u8 c = settings_get()->input[MIDI_PORT_TRS].vel_curve; snprintf(b, n, "%s", vel_names[c < VEL_CURVE_COUNT ? c : 0]); }
static void a_ivc(int d) { int c = settings_get()->input[MIDI_PORT_TRS].vel_curve + (d > 0 ? 1 : VEL_CURVE_COUNT - 1); settings_get()->input[MIDI_PORT_TRS].vel_curve = (u8)(c % VEL_CURVE_COUNT); in_store(); }
static void v_ivf(char *b, int n) { snprintf(b, n, "%d", settings_get()->input[MIDI_PORT_TRS].vel_fixed); }
static void a_ivf(int d) { int v = settings_get()->input[MIDI_PORT_TRS].vel_fixed + (d > 0 ? 5 : -5); if (v < 1) v = 1; if (v > 127) v = 127; settings_get()->input[MIDI_PORT_TRS].vel_fixed = (u8)v; in_store(); }
static const ui_item in_items[] = {
    { "TRS transpose",       UI_ENUM, v_itr, a_itr, NULL, NULL },
    { "TRS velocity curve",  UI_ENUM, v_ivc, a_ivc, NULL, NULL },
    { "TRS fixed velocity",  UI_ENUM, v_ivf, a_ivf, NULL, NULL },
};
static ui_menu in_menu = { in_items, 3, 0, 0 };
static void in_enter(void) { ui_menu_reset(&in_menu); }
static void in_draw(void) { ui_menu_draw(&in_menu, "Inputs"); }
static void in_event(const mx_event *e) { ui_menu_event(&in_menu, e); }
static const ui_page page_inputs = { "Inputs", in_enter, in_draw, in_event, NULL };

/* ------------------------------------------------------------------ log */
static void log_enter(void) { midilog_invalidate(); }
static void log_draw(void) { midilog_draw(); }
static void log_tick(void)
{
    midilog_status_from(bridge_router(), usb_midi_configured(0), ble_midi_ready());
    midilog_draw();
}
static void log_event(const mx_event *e)
{
    if (e->type != MX_EV_BTN_UP || (e->flags & MX_UP_WAS_HELD)) return;
    if (e->id == MX_BTN_OCT_MINUS) ui_back();
    if (e->id == MX_BTN_OCT_PLUS) midilog_set_decoded(!midilog_decoded());
}
static const ui_page page_log = { "MIDI log", log_enter, log_draw, log_event, log_tick };

/* ------------------------------------------------------------------ composer */
static compose_spec comp = { COMPOSE_NOTE, 0, 60, 100, COMPOSE_TO_BOTH };
static u32 note_off_due;                /* ms; 0 = none pending */
static void comp_send_spec(const compose_spec *c)
{
    midi_msg m;
    if (!compose_build(c, &m)) return;
    if (c->port == COMPOSE_TO_USB || c->port == COMPOSE_TO_BOTH) bridge_send_to(MIDI_PORT_USB, &m);
    if (c->port == COMPOSE_TO_BLE || c->port == COMPOSE_TO_BOTH) bridge_send_to(MIDI_PORT_BLE, &m);
}
static void comp_send(void)
{
    comp_send_spec(&comp);
    if (comp.kind == COMPOSE_NOTE) note_off_due = timer_get_ms() + 250;
}
static void comp_tick(void)
{
    if (note_off_due && (s32)(timer_get_ms() - note_off_due) >= 0) {
        midi_msg m;
        note_off_due = 0;
        if (compose_note_off(&comp, &m)) {
            if (comp.port == COMPOSE_TO_USB || comp.port == COMPOSE_TO_BOTH) bridge_send_to(MIDI_PORT_USB, &m);
            if (comp.port == COMPOSE_TO_BLE || comp.port == COMPOSE_TO_BOTH) bridge_send_to(MIDI_PORT_BLE, &m);
        }
    }
}
static void v_ckind(char *b, int n) { snprintf(b, n, "%s", compose_kind_name(comp.kind)); }
static void a_ckind(int d) { comp.kind = (u8)((comp.kind + (d > 0 ? 1 : COMPOSE_KIND_COUNT - 1)) % COMPOSE_KIND_COUNT); }
static void v_cch(char *b, int n) { if (comp.kind == COMPOSE_RAW) snprintf(b, n, "byte3 %d", comp.channel); else if (compose_uses_channel(comp.kind)) snprintf(b, n, "%d", comp.channel + 1); else b[0] = 0; }
static void a_cch(int d) { int v = comp.channel + (d > 0 ? 1 : -1); int max = comp.kind == COMPOSE_RAW ? 127 : 15; if (v < 0) v = max; if (v > max) v = 0; comp.channel = (u8)v; }
static void v_cd1(char *b, int n)
{
    char nb[8];
    if (!compose_uses_d1(comp.kind)) { b[0] = 0; return; }
    if (comp.kind == COMPOSE_RAW) snprintf(b, n, "status %02X", comp.d1);
    else if (comp.kind == COMPOSE_NOTE || comp.kind == COMPOSE_POLYAT) snprintf(b, n, "%d %s", comp.d1, midi_note_name(comp.d1, nb, sizeof nb));
    else if (comp.kind == COMPOSE_CC) snprintf(b, n, "%d %s", comp.d1, midi_cc_name(comp.d1));
    else if (comp.kind == COMPOSE_PROGRAM) snprintf(b, n, "%d", comp.d1 + 1);
    else snprintf(b, n, "%d", comp.d1);
}
static void a_cd1(int d) { int v = comp.d1 + d; int max = comp.kind == COMPOSE_RAW ? 255 : 127; if (v < 0) v = 0; if (v > max) v = max; comp.d1 = (u8)v; }
static void v_cd2(char *b, int n) { if (!compose_uses_d2(comp.kind)) { b[0] = 0; return; } snprintf(b, n, "%d", comp.d2); }
static void a_cd2(int d) { int v = comp.d2 + d; if (v < 0) v = 0; if (v > 127) v = 127; comp.d2 = (u8)v; }
static void v_cport(char *b, int n) { snprintf(b, n, "%s", compose_port_name(comp.port)); }
static void a_cport(int d) { comp.port = (u8)((comp.port + (d > 0 ? 1 : COMPOSE_PORT_COUNT - 1)) % COMPOSE_PORT_COUNT); }
static void v_cprev(char *b, int n) { midi_msg m; if (compose_build(&comp, &m)) midi_decode(&m, b, n); else snprintf(b, n, "invalid"); }
static void fav_text(char *b, int n, int i)
{
    const compose_spec *f = (const compose_spec *)&settings_get()->fav[i];
    midi_msg m;
    if (f->kind >= COMPOSE_KIND_COUNT || !compose_build(f, &m)) { snprintf(b, n, "empty"); return; }
    midi_decode(&m, b, n);
}
static void fav_send(int i) { const compose_spec *f = (const compose_spec *)&settings_get()->fav[i]; if (f->kind < COMPOSE_KIND_COUNT) comp_send_spec(f); }
static void fav_store(int i) { memcpy(&settings_get()->fav[i], &comp, sizeof comp); mark(); }
#define FAV(N) \
    static void v_f##N(char *b, int n) { fav_text(b, n, N); } \
    static void s_f##N(void) { fav_send(N); } \
    static void t_f##N(void) { fav_store(N); }
FAV(0) FAV(1) FAV(2) FAV(3)
static const ui_item comp_items[] = {
    { "Kind",      UI_ENUM,   v_ckind, a_ckind, NULL, NULL },
    { "Channel",   UI_ENUM,   v_cch,   a_cch,   NULL, NULL },
    { "Data 1",    UI_ENUM,   v_cd1,   a_cd1,   NULL, NULL },
    { "Data 2",    UI_ENUM,   v_cd2,   a_cd2,   NULL, NULL },
    { "To",        UI_ENUM,   v_cport, a_cport, NULL, NULL },
    { "Send",      UI_ACTION, v_cprev, NULL, comp_send, NULL },
    { "",          UI_INFO,   NULL, NULL, NULL, NULL },
    { "Fav 1 send", UI_ACTION, v_f0, NULL, s_f0, NULL }, { "  store here", UI_ACTION, NULL, NULL, t_f0, NULL },
    { "Fav 2 send", UI_ACTION, v_f1, NULL, s_f1, NULL }, { "  store here", UI_ACTION, NULL, NULL, t_f1, NULL },
    { "Fav 3 send", UI_ACTION, v_f2, NULL, s_f2, NULL }, { "  store here", UI_ACTION, NULL, NULL, t_f2, NULL },
    { "Fav 4 send", UI_ACTION, v_f3, NULL, s_f3, NULL }, { "  store here", UI_ACTION, NULL, NULL, t_f3, NULL },
};
static ui_menu comp_menu = { comp_items, 15, 0, 0 };
static void comp_enter(void) { ui_menu_reset(&comp_menu); }
static void comp_draw(void) { ui_menu_draw(&comp_menu, "Composer"); ui_row(UI_ROWS - 1, "KNOB1:value OCT+:send/cycle", UI_C_DIM, UI_C_BG); }
static void comp_event(const mx_event *e) { ui_menu_event(&comp_menu, e); }
static const ui_page page_composer = { "Composer", comp_enter, comp_draw, comp_event, comp_tick };

/* ------------------------------------------------------------------ connections */
static u32 conn_last[MIDI_PORT_COUNT][2];
static void conn_draw(void)
{
    midi_router *r = bridge_router();
    char l[UI_COLS + 1];
    int i, row = 1;
    static const struct { int src, dst; } rt[6] = { {0,1}, {0,2}, {1,2}, {2,1}, {3,1}, {3,2} };
    ui_row_inv(0, " Connections");
    snprintf(l, sizeof l, " TRS  jack  in %-6u     %s", (unsigned)r->count_in[MIDI_PORT_TRS], r->count_in[MIDI_PORT_TRS] != conn_last[0][0] ? "*" : "");
    ui_row((u16)row++, l, UI_C_TXT, UI_C_BG);
    snprintf(l, sizeof l, " USB  %-5s in %-6u out %-5u%s", usb_midi_configured(0) ? "ok" : "--", (unsigned)r->count_in[MIDI_PORT_USB], (unsigned)r->count_out[MIDI_PORT_USB],
             (r->count_in[MIDI_PORT_USB] + r->count_out[MIDI_PORT_USB]) != conn_last[1][0] ? "*" : "");
    ui_row((u16)row++, l, usb_midi_configured(0) ? UI_C_OK : UI_C_DIM, UI_C_BG);
    snprintf(l, sizeof l, " BLE  %-5s in %-6u out %-5u%s", ble_port_ready() ? "ok" : ble_host_active() ? "host" : "adv", (unsigned)r->count_in[MIDI_PORT_BLE], (unsigned)r->count_out[MIDI_PORT_BLE],
             (r->count_in[MIDI_PORT_BLE] + r->count_out[MIDI_PORT_BLE]) != conn_last[2][0] ? "*" : "");
    ui_row((u16)row++, l, ble_port_ready() ? UI_C_OK : UI_C_DIM, UI_C_BG);
    snprintf(l, sizeof l, " KBD  %-5s in %-6u     %s", settings_get()->kbd_enabled ? "on" : "off", (unsigned)r->count_in[MIDI_PORT_KBD], r->count_in[MIDI_PORT_KBD] != conn_last[3][0] ? "*" : "");
    ui_row((u16)row++, l, UI_C_TXT, UI_C_BG);
    conn_last[0][0] = r->count_in[MIDI_PORT_TRS]; conn_last[1][0] = r->count_in[MIDI_PORT_USB] + r->count_out[MIDI_PORT_USB];
    conn_last[2][0] = r->count_in[MIDI_PORT_BLE] + r->count_out[MIDI_PORT_BLE]; conn_last[3][0] = r->count_in[MIDI_PORT_KBD];
    row++;
    ui_row((u16)row++, " Routes            on   opts", UI_C_DIM, UI_C_BG);
    for (i = 0; i < 6; i++) {
        const route_opt *o = router_opt(r, rt[i].src, rt[i].dst);
        int on = router_enabled(r, rt[i].src, rt[i].dst);
        snprintf(l, sizeof l, "  %s -> %s        %-4s %s%s%s", router_port_name(rt[i].src), router_port_name(rt[i].dst), on ? "on" : "off",
                 o->channels != 0xFFFF ? "ch " : "", o->remap ? "map " : "", !(o->flags & ROUTE_OPT_CLOCK) ? "noclk" : "");
        ui_row((u16)row++, l, on ? UI_C_TXT : UI_C_DIM, UI_C_BG);
    }
    snprintf(l, sizeof l, " filters %s%s%s  dropped %u", (r->filters & FILTER_DROP_CLOCK) ? "clk " : "", (r->filters & FILTER_DROP_ACTIVE_SENSE) ? "as " : "", (r->filters & FILTER_DROP_SYSEX) ? "sx " : "", (unsigned)r->count_dropped);
    ui_row((u16)row++, l, UI_C_DIM, UI_C_BG);
    while (row < UI_ROWS - 1) ui_row((u16)row++, "", UI_C_TXT, UI_C_BG);
    ui_row(UI_ROWS - 1, "* = activity   OCT-:back", UI_C_DIM, UI_C_BG);
}
static void conn_event(const mx_event *e) { if (e->type == MX_EV_BTN_UP && !(e->flags & MX_UP_WAS_HELD) && e->id == MX_BTN_OCT_MINUS) ui_back(); }
static void conn_tick(void) { static u8 k; if (++k >= 3) { k = 0; ui_invalidate(); } }
static const ui_page page_connections = { "Connections", NULL, conn_draw, conn_event, conn_tick };

/* ------------------------------------------------------------------ statistics */
static void stats_draw(void)
{
    midi_stats *st = bridge_stats();
    char l[UI_COLS + 1], nb[8], nb2[8];
    int i, row = 1;
    ui_row_inv(0, " Statistics       OCT+:reset");
    for (i = 0; i < ST_KIND_COUNT; i++) {
        snprintf(l, sizeof l, " %-11s %10u", stats_kind_name(i), (unsigned)st->kind[i]);
        ui_row((u16)row++, l, st->kind[i] ? UI_C_TXT : UI_C_DIM, UI_C_BG);
    }
    if (st->note_min <= st->note_max && st->note_min != 255)
        snprintf(l, sizeof l, " notes %s..%s  vel %d..%d", midi_note_name(st->note_min, nb, sizeof nb), midi_note_name(st->note_max, nb2, sizeof nb2), st->vel_min, st->vel_max);
    else snprintf(l, sizeof l, " notes: none yet");
    ui_row((u16)row++, l, UI_C_TXT, UI_C_BG);
    if (st->bpm_x10) snprintf(l, sizeof l, " clock tempo %d.%d bpm", st->bpm_x10 / 10, st->bpm_x10 % 10);
    else snprintf(l, sizeof l, " clock tempo: n/a");
    ui_row((u16)row++, l, UI_C_TXT, UI_C_BG);
    {   /* channels seen, as a 16-slot strip */
        int n = snprintf(l, sizeof l, " ch ");
        for (i = 0; i < 16 && n < UI_COLS; i++) n += snprintf(l + n, sizeof l - n, "%c", st->channel[i] ? (i < 9 ? '1' + i : (i == 9 ? '0' : 'a' + i - 10)) : '.');
        ui_row((u16)row++, l, UI_C_DIM, UI_C_BG);
        ui_row((u16)row++, " (1-9, 0 = 10, a-f = 11-16)", UI_C_DIM, UI_C_BG);
    }
    while (row < UI_ROWS - 1) ui_row((u16)row++, "", UI_C_TXT, UI_C_BG);
    ui_row(UI_ROWS - 1, "OCT-:back", UI_C_DIM, UI_C_BG);
}
static void stats_event(const mx_event *e)
{
    if (e->type != MX_EV_BTN_UP || (e->flags & MX_UP_WAS_HELD)) return;
    if (e->id == MX_BTN_OCT_MINUS) ui_back();
    if (e->id == MX_BTN_OCT_PLUS) { bridge_stats_reset(); ui_invalidate(); }
}
static void stats_tick(void) { static u8 k; if (++k >= 5) { k = 0; ui_invalidate(); } }
static const ui_page page_stats = { "Statistics", NULL, stats_draw, stats_event, stats_tick };

/* ------------------------------------------------------------------ recorder */
static u8 rec_loop = 1, rec_to = 3;
static void v_rec_state(char *b, int n)
{
    midi_recorder *r = bridge_recorder();
    if (r->recording) snprintf(b, n, "REC %u ev", r->count);
    else if (r->playing) snprintf(b, n, "PLAY %u/%u", r->play_pos, r->count);
    else if (r->count) snprintf(b, n, "%u ev %u.%us", r->count, (unsigned)(rec_duration(r) / 1000), (unsigned)(rec_duration(r) % 1000 / 100));
    else snprintf(b, n, "empty");
}
static void act_rec(void) { midi_recorder *r = bridge_recorder(); if (r->recording) rec_stop(r, timer_get_ms()); else rec_start(r, timer_get_ms()); }
static void act_play(void) { midi_recorder *r = bridge_recorder(); if (r->playing) rec_play_stop(r); else rec_play(r, timer_get_ms(), rec_loop); }
static void act_rec_clear(void) { rec_clear(bridge_recorder()); }
static void v_rec_loop(char *b, int n) { onoff(b, n, rec_loop); }
static void a_rec_loop(int d) { (void)d; rec_loop = !rec_loop; bridge_recorder()->loop = rec_loop; }
static void v_rec_to(char *b, int n) { snprintf(b, n, "%s", rec_to == 1 ? "USB" : rec_to == 2 ? "BLE" : "USB+BLE"); }
static void a_rec_to(int d) { (void)d; rec_to = (u8)(rec_to % 3 + 1); bridge_recorder_ports(rec_to); }
static void v_rec_play(char *b, int n) { snprintf(b, n, "%s", bridge_recorder()->playing ? "stop" : "start"); }
static void v_rec_rec(char *b, int n) { snprintf(b, n, "%s", bridge_recorder()->recording ? "stop" : "start"); }
static const ui_item rec_items[] = {
    { "State",      UI_INFO,   v_rec_state, NULL, NULL, NULL },
    { "Record",     UI_ACTION, v_rec_rec, NULL, act_rec, NULL },
    { "Play",       UI_ACTION, v_rec_play, NULL, act_play, NULL },
    { "Loop",       UI_TOGGLE, v_rec_loop, a_rec_loop, NULL, NULL },
    { "Play to",    UI_ENUM,   v_rec_to, a_rec_to, NULL, NULL },
    { "Clear",      UI_ACTION, NULL, NULL, act_rec_clear, NULL },
    { "",           UI_INFO,   NULL, NULL, NULL, NULL },
    { "Records what the inputs send", UI_INFO, NULL, NULL, NULL, NULL },
    { "(TRS, USB, BLE), up to 2048", UI_INFO, NULL, NULL, NULL, NULL },
    { "events. REC toggles recording.", UI_INFO, NULL, NULL, NULL, NULL },
};
static ui_menu rec_menu = { rec_items, 10, 0, 0 };
static void rec_enter(void) { ui_menu_reset(&rec_menu); }
static void rec_draw(void) { ui_menu_draw(&rec_menu, "Recorder"); }
static void recpage_event(const mx_event *e)
{
    if (e->type == MX_EV_BTN_UP && !(e->flags & MX_UP_WAS_HELD) && e->id == MX_BTN_REC && settings_get()->btn[BTN_SLOT_REC].type == BTN_MAP_NONE) { act_rec(); ui_invalidate(); return; }
    if (e->type == MX_EV_BTN_UP && !(e->flags & MX_UP_WAS_HELD) && e->id == MX_BTN_PLAY && settings_get()->btn[BTN_SLOT_PLAY].type == BTN_MAP_NONE) { act_play(); ui_invalidate(); return; }
    ui_menu_event(&rec_menu, e);
}
static void rec_tick(void) { static u8 k; if (++k >= 3) { k = 0; ui_invalidate(); } }
static const ui_page page_recorder = { "Recorder", rec_enter, rec_draw, recpage_event, rec_tick };

/* ------------------------------------------------------------------ reference */
static const char *const ref_lines[] = {
    "STATUS BYTES (n = channel 0-F)",
    " 8n note off   note  vel",
    " 9n note on    note  vel",
    "    (vel 0 = note off)",
    " An poly AT    note  pres",
    " Bn control    cc#   value",
    " Cn program    prog",
    " Dn chan AT    pres",
    " En pitch bend lsb   msb",
    "    (00 40 = centre)",
    " F0 .. F7      SysEx",
    " F1 MTC  F2 song pos  F3 sel",
    " F6 tune request",
    " F8 clock (24 per quarter)",
    " FA start FB continue FC stop",
    " FE active sensing  FF reset",
    "",
    "RUNNING STATUS: after a channel",
    "message, more data bytes may",
    "follow without the status byte.",
    "Realtime bytes (F8-FF) can",
    "appear anywhere, even inside",
    "another message.",
    "",
    "COMMON CONTROLLERS",
    "  0 bank msb     32 bank lsb",
    "  1 mod wheel    2 breath",
    "  7 volume      10 pan",
    " 11 expression  64 sustain",
    " 65 portamento  66 sostenuto",
    " 67 soft        71 resonance",
    " 74 cutoff     120 all sound off",
    "121 reset ctrls 123 all notes off",
    "",
    "NOTE NUMBERS: C4 = 60 (middle C)",
    " C-1 = 0, A4 = 69 (440 Hz),",
    " G9 = 127. Octave = 12.",
    "",
    "DEVICE INQUIRY (universal SysEx)",
    " F0 7E 7F 06 01 F7   ask",
    " F0 7E dd 06 02 mfr family",
    "    member version F7  reply",
    "",
    "THIS DEVICE: TRS in 31250 baud,",
    "USB-MIDI 4-byte packets, BLE",
    "MIDI packets with timestamps.",
};
#define REF_LINES ((int)(sizeof ref_lines / sizeof ref_lines[0]))
static int ref_top;
static void ref_enter(void) { ref_top = 0; }
static void ref_draw(void)
{
    int i;
    ui_row_inv(0, " MIDI reference    SEL:scroll");
    for (i = 0; i < UI_ROWS - 2; i++) {
        int k = ref_top + i;
        ui_row((u16)(1 + i), k < REF_LINES ? ref_lines[k] : "", k < REF_LINES && ref_lines[k][0] != ' ' && ref_lines[k][0] ? UI_C_OK : UI_C_TXT, UI_C_BG);
    }
    ui_row(UI_ROWS - 1, "OCT-:back", UI_C_DIM, UI_C_BG);
}
static void ref_event(const mx_event *e)
{
    if (e->type == MX_EV_ENC && (e->id == MX_ENC_SELECT || e->id == MX_ENC_PRESETS)) {
        ref_top += e->delta > 0 ? 3 : -3;
        if (ref_top < 0) ref_top = 0;
        if (ref_top > REF_LINES - (UI_ROWS - 2)) ref_top = REF_LINES - (UI_ROWS - 2);
        ui_invalidate();
    }
    if (e->type == MX_EV_BTN_UP && !(e->flags & MX_UP_WAS_HELD) && e->id == MX_BTN_OCT_MINUS) ui_back();
}
static const ui_page page_reference = { "MIDI reference", ref_enter, ref_draw, ref_event, NULL };

/* ------------------------------------------------------------------ clock master */
static void v_clk_run(char *b, int n) { snprintf(b, n, "%s", bridge_clock_running() ? "running" : "stopped"); }
static void act_clk(void) { bridge_clock_run(!bridge_clock_running()); }
static void v_clk_bpm(char *b, int n) { snprintf(b, n, "%u", settings_get()->clock_bpm); }
static void a_clk_bpm(int d) { int v = settings_get()->clock_bpm + d; if (v < 20) v = 20; if (v > 300) v = 300; settings_get()->clock_bpm = (u16)v; mark(); }
static void v_clk_to(char *b, int n) { u8 t = settings_get()->clock_to; snprintf(b, n, "%s", t == 1 ? "USB" : t == 2 ? "BLE" : "USB+BLE"); }
static void a_clk_to(int d) { (void)d; settings_get()->clock_to = (u8)(settings_get()->clock_to % 3 + 1); mark(); }
static const ui_item clk_items[] = {
    { "Clock",        UI_ACTION, v_clk_run, NULL, act_clk, NULL },
    { "Tempo (bpm)",  UI_ENUM,   v_clk_bpm, a_clk_bpm, NULL, NULL },
    { "Send to",      UI_ENUM,   v_clk_to,  a_clk_to,  NULL, NULL },
    { "",             UI_INFO,   NULL, NULL, NULL, NULL },
    { "Sends F8 at 24 per quarter,", UI_INFO, NULL, NULL, NULL, NULL },
    { "Start on run, Stop on stop.", UI_INFO, NULL, NULL, NULL, NULL },
    { "PLAY toggles it on this page.", UI_INFO, NULL, NULL, NULL, NULL },
};
static ui_menu clk_menu = { clk_items, 7, 0, 0 };
static void clk_enter(void) { ui_menu_reset(&clk_menu); }
static void clk_draw(void) { ui_menu_draw(&clk_menu, "Clock master"); }
static void clk_event(const mx_event *e)
{
    if (e->type == MX_EV_BTN_UP && !(e->flags & MX_UP_WAS_HELD) && e->id == MX_BTN_PLAY && settings_get()->btn[BTN_SLOT_PLAY].type == BTN_MAP_NONE) { act_clk(); ui_invalidate(); return; }
    ui_menu_event(&clk_menu, e);
}
static void clk_tick(void) { static u8 k; if (++k >= 5) { k = 0; ui_invalidate(); } }
static const ui_page page_clock = { "Clock master", clk_enter, clk_draw, clk_event, clk_tick };

/* ------------------------------------------------------------------ bluetooth */
static u8 pairable = 1;
static void v_ble_state(char *b, int n)
{
    if (!ble_host_active() && !ble_midi_ready() && !bt_ble_is_connected() && !pairable) snprintf(b, n, "idle");
    else snprintf(b, n, "%s", ble_port_state_text());
}
static void v_pairable(char *b, int n) { onoff(b, n, pairable); }
static void a_pairable(int d) { (void)d; if (ble_host_active()) return; pairable = !pairable; bt_ble_adv_enable(pairable); }
static void v_link(char *b, int n) { snprintf(b, n, "%s", settings_get()->ble_link_mode == BLE_LINK_POWER_SAVING ? "power saving" : "low latency"); }
static void a_link(int d) { (void)d; settings_get()->ble_link_mode = !settings_get()->ble_link_mode; mark(); ble_port_set_link_mode(settings_get()->ble_link_mode); }
static void v_hostauto(char *b, int n) { onoff(b, n, settings_get()->ble_host_auto); }
static void a_hostauto(int d) { (void)d; settings_get()->ble_host_auto = !settings_get()->ble_host_auto; mark(); }
static void v_hostpair(char *b, int n) { onoff(b, n, settings_get()->ble_host_pair); }
static void a_hostpair(int d) { (void)d; settings_get()->ble_host_pair = !settings_get()->ble_host_pair; mark(); }
static void act_disconnect(void) { ble_app_disconnect(); }
static void do_forget(void) { ble_app_disconnect(); ble_list_clear_all(); }
static void act_forget(void) { ui_confirm("Forget all paired devices?", do_forget); }
static void v_bonds(char *b, int n) { snprintf(b, n, "%u", ble_list_get_count()); }
static void v_name(char *b, int n) { snprintf(b, n, "%s", BLE_MIDI_DEVICE_NAME); }
static void v_host(char *b, int n) { onoff(b, n, settings_get()->ble_mode == BLE_MODE_HOST); }
static void a_host(int d) { (void)d; settings_get()->ble_mode = !settings_get()->ble_mode; mark(); ble_port_set_mode(settings_get()->ble_mode); }
static int host_on(void) { return settings_get()->ble_mode == BLE_MODE_HOST; }
static void go_scan(void) { ui_goto(PAGE_BLE_SCAN); }
static const ui_item ble_items[] = {
    { "State",         UI_INFO,    v_ble_state, NULL, NULL, NULL },
    { "Name",          UI_INFO,    v_name, NULL, NULL, NULL },
    { "Pairable",      UI_TOGGLE,  v_pairable, a_pairable, NULL, NULL },
    { "Link",          UI_ENUM,    v_link, a_link, NULL, NULL },
    { "Disconnect",    UI_ACTION,  NULL, NULL, act_disconnect, NULL },
    { "Forget bonds",  UI_ACTION,  v_bonds, NULL, act_forget, NULL },
    { "",              UI_INFO,    NULL, NULL, NULL, NULL },
    { "Host mode",     UI_TOGGLE,  v_host, a_host, NULL, NULL },
    { "Scan devices",  UI_SUBMENU, NULL, NULL, go_scan, host_on },
    { "Pair on connect", UI_TOGGLE, v_hostpair, a_hostpair, NULL, host_on },
    { "Auto reconnect", UI_TOGGLE, v_hostauto, a_hostauto, NULL, host_on },
};
static ui_menu ble_menu = { ble_items, 11, 0, 0 };
static void ble_enter(void) { ui_menu_reset(&ble_menu); }
static void ble_draw(void) { ui_menu_draw(&ble_menu, "Bluetooth"); }
static void ble_event(const mx_event *e) { ui_menu_event(&ble_menu, e); }
static void ble_tick(void) { static u8 k; if (++k >= 5) { k = 0; ui_invalidate(); } }
static const ui_page page_ble = { "Bluetooth", ble_enter, ble_draw, ble_event, ble_tick };

/* scan page: the devices found, newest state on row 1 */
static u8 scan_cursor;
static void scan_enter(void) { scan_cursor = 0; ble_host_scan(1); }
static void scan_draw(void)
{
    ble_host_device list[BLE_HOST_MAX_DEVICES];
    char l[UI_COLS + 1];
    int n = ble_host_devices(list, BLE_HOST_MAX_DEVICES), i, row = 2;
    ui_row_inv(0, " Scan devices");
    snprintf(l, sizeof l, " %-21s %d found", ble_host_state_text() + 6, n);
    ui_row(1, l, UI_C_DIM, UI_C_BG);
    if (scan_cursor >= n) scan_cursor = n ? (u8)(n - 1) : 0;
    for (i = 0; i < n; i++, row++) {
        char who[22];
        if (list[i].name[0]) snprintf(who, sizeof who, "%s", list[i].name);
        else snprintf(who, sizeof who, "%02X:%02X:%02X:%02X:%02X:%02X", list[i].addr[5], list[i].addr[4], list[i].addr[3], list[i].addr[2], list[i].addr[1], list[i].addr[0]);
        snprintf(l, sizeof l, " %-20s %4ddBm", who, list[i].rssi);
        if (i == scan_cursor) ui_row_inv((u16)row, l); else ui_row((u16)row, l, UI_C_TXT, UI_C_BG);
    }
    while (row < UI_ROWS - 1) ui_row((u16)row++, "", UI_C_TXT, UI_C_BG);
    ui_row(UI_ROWS - 1, "SEL OCT+:connect OCT-:back", UI_C_DIM, UI_C_BG);
}
static void scan_event(const mx_event *e)
{
    if (e->type == MX_EV_ENC && (e->id == MX_ENC_SELECT || e->id == MX_ENC_PRESETS)) {
        if (e->delta > 0) scan_cursor++; else if (scan_cursor) scan_cursor--;
        ui_invalidate();
    }
    if (e->type != MX_EV_BTN_UP || (e->flags & MX_UP_WAS_HELD)) return;
    if (e->id == MX_BTN_OCT_MINUS) { ble_host_scan(0); ui_back(); }
    if (e->id == MX_BTN_OCT_PLUS) { ble_host_connect(scan_cursor); ui_invalidate(); }
}
static void scan_tick(void) { static u8 k; if (++k >= 5) { k = 0; ui_invalidate(); } }
static const ui_page page_scan = { "Scan devices", scan_enter, scan_draw, scan_event, scan_tick };

/* ------------------------------------------------------------------ usb */
extern u32 usb_otg_online(const usb_dev id);
static void v_cable(char *b, int n) { snprintf(b, n, "%s", usb_otg_online(0) == SLAVE_MODE ? "yes" : "no"); }
static void v_enum(char *b, int n) { snprintf(b, n, "%s", usb_midi_configured(0) ? "enumerated" : "--"); }
static void v_usb_en(char *b, int n) { onoff(b, n, settings_get()->usb_enabled); }
static void a_usb_en(int d) { (void)d; settings_get()->usb_enabled = !settings_get()->usb_enabled; mark(); usb_port_apply(); }
static const ui_item usb_items[] = {
    { "Cable",         UI_INFO,   v_cable, NULL, NULL, NULL },
    { "Host",          UI_INFO,   v_enum, NULL, NULL, NULL },
    { "USB MIDI",      UI_TOGGLE, v_usb_en, a_usb_en, NULL, NULL },
    { "USB in/out",    UI_INFO,   v_cnt_usb, NULL, NULL, NULL },
};
static ui_menu usb_menu = { usb_items, 4, 0, 0 };
static void usb_enter(void) { ui_menu_reset(&usb_menu); }
static void usb_draw(void) { ui_menu_draw(&usb_menu, "USB"); }
static void usb_event(const mx_event *e) { ui_menu_event(&usb_menu, e); }
static void usb_tick(void) { static u8 k; if (++k >= 5) { k = 0; ui_invalidate(); } }
static const ui_page page_usb = { "USB", usb_enter, usb_draw, usb_event, usb_tick };

/* ------------------------------------------------------------------ filters */
static void filt_adj(u8 bit) { settings_get()->filters ^= bit; bridge_router()->filters = settings_get()->filters; mark(); }
static void v_fclk(char *b, int n) { onoff(b, n, settings_get()->filters & FILTER_DROP_CLOCK); }
static void v_fas(char *b, int n)  { onoff(b, n, settings_get()->filters & FILTER_DROP_ACTIVE_SENSE); }
static void v_fsx(char *b, int n)  { onoff(b, n, settings_get()->filters & FILTER_DROP_SYSEX); }
static void a_fclk(int d) { (void)d; filt_adj(FILTER_DROP_CLOCK); }
static void a_fas(int d)  { (void)d; filt_adj(FILTER_DROP_ACTIVE_SENSE); }
static void a_fsx(int d)  { (void)d; filt_adj(FILTER_DROP_SYSEX); }
static void v_dropped(char *b, int n) { snprintf(b, n, "%u", (unsigned)bridge_router()->count_dropped); }
static void v_mg(char *b, int n) { onoff(b, n, settings_get()->merge_guard); }
static void a_mg(int d) { (void)d; settings_get()->merge_guard = !settings_get()->merge_guard; mark(); }
static void v_pod(char *b, int n) { onoff(b, n, settings_get()->panic_on_disconnect); }
static void a_pod(int d) { (void)d; settings_get()->panic_on_disconnect = !settings_get()->panic_on_disconnect; mark(); }
static void v_panic(char *b, int n) { snprintf(b, n, "%s", bridge_panic_active() ? "sending..." : ""); }
static void act_panic(void) { bridge_panic_request(); }
static const ui_item filt_items[] = {
    { "Drop clock (F8)",     UI_TOGGLE, v_fclk, a_fclk, NULL, NULL },
    { "Drop active sense",   UI_TOGGLE, v_fas,  a_fas,  NULL, NULL },
    { "Drop SysEx",          UI_TOGGLE, v_fsx,  a_fsx,  NULL, NULL },
    { "Dropped",             UI_INFO,   v_dropped, NULL, NULL, NULL },
    { "",                    UI_INFO,   NULL, NULL, NULL, NULL },
    { "Panic (all off)",     UI_ACTION, v_panic, NULL, act_panic, NULL },
    { "Panic on link loss",  UI_TOGGLE, v_pod, a_pod, NULL, NULL },
    { "Note collision guard",UI_TOGGLE, v_mg,  a_mg,  NULL, NULL },
};
static ui_menu filt_menu = { filt_items, 8, 0, 0 };
static void filt_enter(void) { ui_menu_reset(&filt_menu); }
static void filt_draw(void) { ui_menu_draw(&filt_menu, "MIDI filters"); }
static void filt_event(const mx_event *e) { ui_menu_event(&filt_menu, e); }
static void filt_tick(void) { static u8 k; if (++k >= 5) { k = 0; ui_invalidate(); } }
static const ui_page page_filters = { "MIDI filters", filt_enter, filt_draw, filt_event, filt_tick };

/* ------------------------------------------------------------------ display */
static const char *const bl_names[BACKLIGHT_COUNT] = { "never", "10 s", "30 s", "1 min", "5 min" };
static void v_bl(char *b, int n) { u8 i = settings_get()->backlight_idx; snprintf(b, n, "%s", bl_names[i < BACKLIGHT_COUNT ? i : 0]); }
static void a_bl(int d) { int i = settings_get()->backlight_idx + (d > 0 ? 1 : -1); if (i < 0) i = BACKLIGHT_COUNT - 1; if (i >= BACKLIGHT_COUNT) i = 0; settings_get()->backlight_idx = (u8)i; mark(); }
static void v_inv(char *b, int n) { onoff(b, n, settings_get()->invert); }
static void a_inv(int d) { (void)d; settings_get()->invert = !settings_get()->invert; lcd_invert(settings_get()->invert); mark(); }
static const ui_item disp_items[] = {
    { "Backlight off after", UI_ENUM,   v_bl,  a_bl,  NULL, NULL },
    { "Invert colours",      UI_TOGGLE, v_inv, a_inv, NULL, NULL },
};
static ui_menu disp_menu = { disp_items, 2, 0, 0 };
static void disp_enter(void) { ui_menu_reset(&disp_menu); }
static void disp_draw(void) { ui_menu_draw(&disp_menu, "Display"); }
static void disp_event(const mx_event *e) { ui_menu_event(&disp_menu, e); }
static const ui_page page_display = { "Display", disp_enter, disp_draw, disp_event, NULL };

/* ------------------------------------------------------------------ system */
static void v_ident(char *b, int n) { snprintf(b, n, "%s", UPD_IDENTITY); }
static void v_ver(char *b, int n) { snprintf(b, n, "v%s", FM1_FW_VERSION); }
static void v_built(char *b, int n) { snprintf(b, n, "%s", __DATE__); }
static void v_bat(char *b, int n) { snprintf(b, n, "%u mV (pin)", bat_mv); }
static void v_up(char *b, int n) { u32 s = timer_get_ms() / 1000; snprintf(b, n, "%u:%02u:%02u", (unsigned)(s / 3600), (unsigned)(s / 60 % 60), (unsigned)(s % 60)); }
static void v_boots(char *b, int n) { snprintf(b, n, "%u", (unsigned)health_get()->boots); }
static void v_reason(char *b, int n) { snprintf(b, n, "%s", health_reason_text(health_get()->last_reason)); }
static void v_crashes(char *b, int n) { snprintf(b, n, "wdt %u hang %u", (unsigned)health_get()->wdt_resets, (unsigned)health_get()->hangs); }
static void act_health_reset(void) { health_reset_counters(); }
static void v_drops(char *b, int n) { extern mx_state *matrix_state(void); snprintf(b, n, "%u", (unsigned)matrix_state()->dropped); }
static void go_input(void) { ui_goto(PAGE_INPUT); }
static void go_presets(void) { ui_goto(PAGE_PRESETS); }
static void do_reboot(void) { cpu_reset(); }
static void act_reboot(void) { ui_confirm("Reboot now?", do_reboot); }
static void do_recovery(void) { upd_port_uboot(); }
static void act_recovery(void) { ui_confirm("Reboot into USB recovery mode?", do_recovery); }
static void do_reset(void) { settings_reset(); settings_apply_router(settings_get(), bridge_router()); lcd_invert(0); ui_keys_reload(); }
static void act_reset(void) { ui_confirm("Reset all settings?", do_reset); }
static const ui_item sys_items[] = {
    { "Firmware",       UI_INFO,   v_ident, NULL, NULL, NULL },
    { "Version",        UI_INFO,   v_ver,   NULL, NULL, NULL },
    { "Built",          UI_INFO,   v_built, NULL, NULL, NULL },
    { "Battery",        UI_INFO,   v_bat,   NULL, NULL, NULL },
    { "Uptime",         UI_INFO,   v_up,    NULL, NULL, NULL },
    { "TRS in/out",     UI_INFO,   v_cnt_trs, NULL, NULL, NULL },
    { "USB in/out",     UI_INFO,   v_cnt_usb, NULL, NULL, NULL },
    { "BLE in/out",     UI_INFO,   v_cnt_ble, NULL, NULL, NULL },
    { "Input drops",    UI_INFO,   v_drops, NULL, NULL, NULL },
    { "Boots",          UI_INFO,   v_boots, NULL, NULL, NULL },
    { "Last reset",     UI_INFO,   v_reason, NULL, NULL, NULL },
    { "Crashes",        UI_INFO,   v_crashes, NULL, NULL, NULL },
    { "Reset crash counters", UI_ACTION, NULL, NULL, act_health_reset, NULL },
    { "",               UI_INFO,   NULL, NULL, NULL, NULL },
    { "Reboot",         UI_ACTION, NULL, NULL, act_reboot, NULL },
    { "USB recovery mode", UI_ACTION, NULL, NULL, act_recovery, NULL },
    { "Reset settings", UI_ACTION, NULL, NULL, act_reset, NULL },
    { "Presets",        UI_SUBMENU, NULL, NULL, go_presets, NULL },
    { "Input test",     UI_SUBMENU, NULL, NULL, go_input, NULL },
};
static ui_menu sys_menu = { sys_items, 19, 0, 0 };
static void sys_enter(void) { ui_menu_reset(&sys_menu); }
static void sys_draw(void) { ui_menu_draw(&sys_menu, "System"); }
static void sys_event(const mx_event *e) { ui_menu_event(&sys_menu, e); }
static void sys_tick(void) { static u8 k; if (++k >= 10) { k = 0; bat_mv = (u16)adc_get_voltage(AD_CH_PB01); ui_invalidate(); } }
static const ui_page page_system = { "System", sys_enter, sys_draw, sys_event, sys_tick };

/* ------------------------------------------------------------------ keyboard */
static void kbd_apply(void) { mark(); ui_keys_reload(); }
static void v_kbd_en(char *b, int n) { onoff(b, n, settings_get()->kbd_enabled); }
static void a_kbd_en(int d) { (void)d; settings_get()->kbd_enabled = !settings_get()->kbd_enabled; kbd_apply(); }
static void v_kbd_ch(char *b, int n) { snprintf(b, n, "%d", settings_get()->kbd_channel + 1); }
static void a_kbd_ch(int d) { settings_get()->kbd_channel = (u8)((settings_get()->kbd_channel + (d > 0 ? 1 : 15)) & 15); kbd_apply(); }
static void v_kbd_oct(char *b, int n) { int o = settings_get()->kbd_octave; snprintf(b, n, "%s%d  (F%d..G%d)", plus(o), o, 3 + o, 5 + o); }
static void a_kbd_oct(int d) { int o = settings_get()->kbd_octave + (d > 0 ? 1 : -1); if (o < -3) o = -3; if (o > 3) o = 3; settings_get()->kbd_octave = (s8)o; kbd_apply(); }
static void v_kbd_vel(char *b, int n) { if (settings_get()->kbd_vel_source) snprintf(b, n, "pot %d", ui_keys()->velocity); else snprintf(b, n, "%d", settings_get()->kbd_velocity); }
static void v_kbd_vs(char *b, int n) { snprintf(b, n, "%s", settings_get()->kbd_vel_source ? "MASTER pot" : "fixed"); }
static void a_kbd_vs(int d) { (void)d; settings_get()->kbd_vel_source = !settings_get()->kbd_vel_source; kbd_apply(); }
static void a_kbd_vel(int d) { int v = settings_get()->kbd_velocity + (d > 0 ? 10 : -10); if (v < 10) v = 10; if (v > 127) v = 127; settings_get()->kbd_velocity = (u8)v; kbd_apply(); }
static void v_prog(char *b, int n) { snprintf(b, n, "%d", ui_keys()->program + 1); }
static void v_bank(char *b, int n) { snprintf(b, n, "%d", ui_keys()->bank); }
static void a_prog(int d) { midi_msg m; if (keys_program_step(ui_keys(), d > 0 ? 1 : -1, &m)) bridge_inject(MIDI_PORT_KBD, &m); }
static void a_bank(int d) { midi_msg m[2]; int n = keys_bank_step(ui_keys(), d > 0 ? 1 : -1, m, 2), i; for (i = 0; i < n; i++) bridge_inject(MIDI_PORT_KBD, &m[i]); }
static void v_knob(char *b, int n, int i) { keys_state *k = ui_keys(); snprintf(b, n, "CC%d = %d", k->knob_cc[i], k->knob_val[i]); }
static void v_knob1(char *b, int n) { v_knob(b, n, 0); }
static void v_knob2(char *b, int n) { v_knob(b, n, 1); }
static void v_knob3(char *b, int n) { v_knob(b, n, 2); }
static void v_knob4(char *b, int n) { v_knob(b, n, 3); }
static const ui_item kbd_items[] = {
    { "Keys send MIDI",  UI_TOGGLE, v_kbd_en,  a_kbd_en,  NULL, NULL },
    { "Channel",         UI_ENUM,   v_kbd_ch,  a_kbd_ch,  NULL, NULL },
    { "Octave  (OCT-/+)",UI_ENUM,   v_kbd_oct, a_kbd_oct, NULL, NULL },
    { "Velocity",        UI_ENUM,   v_kbd_vel, a_kbd_vel, NULL, NULL },
    { "Velocity source", UI_ENUM,   v_kbd_vs,  a_kbd_vs,  NULL, NULL },
    { "Program (PRESETS)", UI_ENUM, v_prog, a_prog, NULL, NULL },
    { "Bank MSB (ALGO)", UI_ENUM,   v_bank, a_bank, NULL, NULL },
    { "",                UI_INFO,   NULL, NULL, NULL, NULL },
    { "KNOB1",           UI_INFO,   v_knob1, NULL, NULL, NULL },
    { "KNOB2",           UI_INFO,   v_knob2, NULL, NULL, NULL },
    { "KNOB3",           UI_INFO,   v_knob3, NULL, NULL, NULL },
    { "KNOB4",           UI_INFO,   v_knob4, NULL, NULL, NULL },
    { "",                UI_INFO,   NULL, NULL, NULL, NULL },
    { "Knobs send CCs on this page", UI_INFO, NULL, NULL, NULL, NULL },
};
static ui_menu kbd_menu = { kbd_items, 14, 0, 0 };
static void kbd_enter(void) { ui_menu_reset(&kbd_menu); }
static void kbd_draw(void) { ui_menu_draw(&kbd_menu, "Keyboard"); ui_row(UI_ROWS - 1, "SEL:move OCT:octave SAVE:back", UI_C_DIM, UI_C_BG); }
static void kbd_event(const mx_event *e)
{
    midi_msg m;
    if (e->type == MX_EV_ENC && e->id == MX_ENC_PRESETS) { a_prog(e->delta); ui_invalidate(); return; }
    if (e->type == MX_EV_ENC && e->id == MX_ENC_ALGO) { a_bank(e->delta); ui_invalidate(); return; }
    if (e->type == MX_EV_ENC && e->id >= MX_ENC_KNOB1 && e->id <= MX_ENC_KNOB4) {
        if (settings_get()->kbd_enabled && keys_knob_event(ui_keys(), e->id - MX_ENC_KNOB1, e->delta, &m)) bridge_inject(MIDI_PORT_KBD, &m);
        ui_invalidate();
        return;
    }
    if (e->type == MX_EV_BTN_UP && !(e->flags & MX_UP_WAS_HELD) && (e->id == MX_BTN_OCT_MINUS || e->id == MX_BTN_OCT_PLUS)) {
        a_kbd_oct(e->id == MX_BTN_OCT_PLUS ? 1 : -1);
        ui_invalidate();
        return;
    }
    if (e->type == MX_EV_BTN_UP && !(e->flags & MX_UP_WAS_HELD) && e->id == MX_BTN_SAVE) { ui_back(); return; }   /* OCT- is taken: SAVE leaves */
    ui_menu_event(&kbd_menu, e);
}
static const ui_page page_keyboard = { "Keyboard", kbd_enter, kbd_draw, kbd_event, NULL };

/* ------------------------------------------------------------------ button MIDI */
static void v_btn(char *b, int n, int slot)
{
    u8 t = settings_get()->btn[slot].type, num = settings_get()->btn[slot].number;
    switch (t) {
    case BTN_MAP_NOTE: snprintf(b, n, "note %d", num); break;
    case BTN_MAP_CC: snprintf(b, n, "CC %d", num); break;
    case BTN_MAP_PROGRAM: snprintf(b, n, "program %d", num + 1); break;
    default: snprintf(b, n, "none"); break;
    }
}
static void cycle_btn(int slot) { settings_get()->btn[slot].type = (u8)((settings_get()->btn[slot].type + 1) % BTN_MAP_COUNT); mark(); }
static void adj_btn(int slot, int d) { int v = settings_get()->btn[slot].number + d; if (v < 0) v = 0; if (v > 127) v = 127; settings_get()->btn[slot].number = (u8)v; mark(); }
/* note keys: the same, on settings->key[]; "none" means the keyboard note */
static int key_cursor;                     /* which key the KEY row edits: chosen with the PRESETS encoder */
static const char *const note_names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
static void v_key(char *b, int n)
{
    u8 t = settings_get()->key[key_cursor].type, num = settings_get()->key[key_cursor].number;
    int note = KEYS_BASE + key_cursor;
    char job[16];
    switch (t) {
    case BTN_MAP_NOTE: snprintf(job, sizeof job, "note %d", num); break;
    case BTN_MAP_CC: snprintf(job, sizeof job, "CC %d", num); break;
    case BTN_MAP_PROGRAM: snprintf(job, sizeof job, "prog %d", num + 1); break;
    default: snprintf(job, sizeof job, "keyboard"); break;
    }
    snprintf(b, n, "%s%d: %s", note_names[note % 12], note / 12 - 1, job);
}
static void c_key(void) { settings_get()->key[key_cursor].type = (u8)((settings_get()->key[key_cursor].type + 1) % BTN_MAP_COUNT); mark(); }
static void a_key(int d) { int v = settings_get()->key[key_cursor].number + d; if (v < 0) v = 0; if (v > 127) v = 127; settings_get()->key[key_cursor].number = (u8)v; mark(); }
#define BTN_ITEM(N, S) \
    static void v_b##N(char *b, int n) { v_btn(b, n, S); } \
    static void c_b##N(void) { cycle_btn(S); } \
    static void a_b##N(int d) { adj_btn(S, d); }
BTN_ITEM(0, 0) BTN_ITEM(1, 1) BTN_ITEM(2, 2) BTN_ITEM(3, 3) BTN_ITEM(4, 4) BTN_ITEM(5, 5) BTN_ITEM(6, 6) BTN_ITEM(7, 7) BTN_ITEM(8, 8)
static const ui_item btn_items[] = {
    { "FX",   UI_ENUM, v_b0, a_b0, c_b0, NULL }, { "SCL",  UI_ENUM, v_b1, a_b1, c_b1, NULL },
    { "ENV",  UI_ENUM, v_b2, a_b2, c_b2, NULL }, { "LFO",  UI_ENUM, v_b3, a_b3, c_b3, NULL },
    { "EDIT", UI_ENUM, v_b4, a_b4, c_b4, NULL }, { "SEQ",  UI_ENUM, v_b5, a_b5, c_b5, NULL },
    { "ARP",  UI_ENUM, v_b6, a_b6, c_b6, NULL }, { "PLAY", UI_ENUM, v_b7, a_b7, c_b7, NULL },
    { "REC",  UI_ENUM, v_b8, a_b8, c_b8, NULL },
    { "", UI_INFO, NULL, NULL, NULL, NULL },
    { "Key (PRESETS picks)", UI_ENUM, v_key, a_key, c_key, NULL },
    { "", UI_INFO, NULL, NULL, NULL, NULL },
    { "OCT+ cycles the kind, KNOB1", UI_INFO, NULL, NULL, NULL, NULL },
    { "sets the number. Channel:", UI_INFO, NULL, NULL, NULL, NULL },
    { "the keyboard's. Or press", UI_INFO, NULL, NULL, NULL, NULL },
    { "a key to pick it.", UI_INFO, NULL, NULL, NULL, NULL },
};
static ui_menu btn_menu = { btn_items, 16, 0, 0 };
static void btn_enter(void) { ui_menu_reset(&btn_menu); }
static void btn_draw(void) { ui_menu_draw(&btn_menu, "Button MIDI"); }
static void btn_event(const mx_event *e)
{
    if (e->type == MX_EV_ENC && e->id == MX_ENC_PRESETS) {
        key_cursor += e->delta > 0 ? 1 : -1;
        if (key_cursor < 0) key_cursor = KEYS_COUNT - 1;
        if (key_cursor >= KEYS_COUNT) key_cursor = 0;
        ui_invalidate();
        return;
    }
    ui_menu_event(&btn_menu, e);
}
/* on this page a pressed key becomes the one being edited (ui.c calls this first) */
int ui_buttons_page_pick(int key)
{
    if (ui_current() != PAGE_BUTTONS) return 0;
    key_cursor = key;
    ui_invalidate();
    return 1;
}
static const ui_page page_buttons = { "Button MIDI", btn_enter, btn_draw, btn_event, NULL };

/* ------------------------------------------------------------------ presets */
static int preset_slot;
static void preset_apply(void) { settings_apply_router(settings_get(), bridge_router()); ui_keys_reload(); ble_port_set_link_mode(settings_get()->ble_link_mode); ble_port_set_mode(settings_get()->ble_mode); usb_port_apply(); lcd_invert(settings_get()->invert); }
static void do_load(void) { if (settings_preset_load(preset_slot) == 0) preset_apply(); }
static void do_save(void) { settings_preset_save(preset_slot); }
static void do_clear(void) { settings_preset_clear(preset_slot); }
static void v_pre(char *b, int n, int slot) { snprintf(b, n, "%s", settings_preset_used(slot) ? "used" : "empty"); }
static int pre_used(int slot) { return settings_preset_used(slot); }
#define PRESET_ITEMS(N) \
    static void v_p##N(char *b, int n) { v_pre(b, n, N); } \
    static void l_p##N(void) { preset_slot = N; ui_confirm("Load preset " #N "? Current settings are replaced.", do_load); } \
    static void s_p##N(void) { preset_slot = N; ui_confirm("Save current settings to preset " #N "?", do_save); } \
    static void c_p##N(void) { preset_slot = N; ui_confirm("Clear preset " #N "?", do_clear); } \
    static int u_p##N(void) { return pre_used(N); }
PRESET_ITEMS(0) PRESET_ITEMS(1) PRESET_ITEMS(2) PRESET_ITEMS(3)
static const ui_item pre_items[] = {
    { "Preset 1",       UI_INFO,   v_p0, NULL, NULL, NULL },
    { "  load",         UI_ACTION, NULL, NULL, l_p0, u_p0 },
    { "  save here",    UI_ACTION, NULL, NULL, s_p0, NULL },
    { "  clear",        UI_ACTION, NULL, NULL, c_p0, u_p0 },
    { "Preset 2",       UI_INFO,   v_p1, NULL, NULL, NULL },
    { "  load",         UI_ACTION, NULL, NULL, l_p1, u_p1 },
    { "  save here",    UI_ACTION, NULL, NULL, s_p1, NULL },
    { "  clear",        UI_ACTION, NULL, NULL, c_p1, u_p1 },
    { "Preset 3",       UI_INFO,   v_p2, NULL, NULL, NULL },
    { "  load",         UI_ACTION, NULL, NULL, l_p2, u_p2 },
    { "  save here",    UI_ACTION, NULL, NULL, s_p2, NULL },
    { "  clear",        UI_ACTION, NULL, NULL, c_p2, u_p2 },
    { "Preset 4",       UI_INFO,   v_p3, NULL, NULL, NULL },
    { "  load",         UI_ACTION, NULL, NULL, l_p3, u_p3 },
    { "  save here",    UI_ACTION, NULL, NULL, s_p3, NULL },
    { "  clear",        UI_ACTION, NULL, NULL, c_p3, u_p3 },
};
static ui_menu pre_menu = { pre_items, 16, 0, 0 };
static void pre_enter(void) { ui_menu_reset(&pre_menu); }
static void pre_draw(void) { ui_menu_draw(&pre_menu, "Presets"); }
static void pre_event(const mx_event *e) { ui_menu_event(&pre_menu, e); }
static const ui_page page_presets = { "Presets", pre_enter, pre_draw, pre_event, NULL };

/* ------------------------------------------------------------------ input test */
/* Shows what the matrix scanner sees, for checking the position tables on a new unit:
 * every closed cell as (column,row), the buttons it decodes, the encoder step counts
 * and the queue drops. HOME leaves (it is the only global button). */
static const char *const btn_names[MX_BTN_KEY0] = {
    "OCT-", "OCT+", "FX", "SCL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ", "PLAY", "REC" };
static const char *const enc_names[MX_ENC_COUNT] = { "SELECT", "ALGO", "KNOB1", "KNOB2", "KNOB3", "KNOB4", "PRESETS" };
static int enc_count[MX_ENC_COUNT];
static char last_event[UI_COLS + 1];
static void btn_name(char *b, int n, int id)
{
    if (id < MX_BTN_KEY0) snprintf(b, n, "%s", btn_names[id]);
    else snprintf(b, n, "key%d", id - MX_BTN_KEY0);
}
static void input_enter(void) { memset(enc_count, 0, sizeof enc_count); last_event[0] = 0; }
static void input_draw(void)
{
    extern mx_state *matrix_state(void);
    mx_state *mx = matrix_state();
    char l[UI_COLS + 1], nm[12];
    int c, r, n, row = 1;
    ui_row_inv(0, " Input test        HOME:leave");
    /* raw closed cells */
    n = snprintf(l, sizeof l, " cells:");
    for (c = 0; c < MX_COLS; c++)
        for (r = 0; r < MX_ROWS; r++)
            if ((mx->raw[c] >> r) & 1 && n < UI_COLS - 5) n += snprintf(l + n, sizeof l - n, " %d,%d", c, r);
    ui_row((u16)row++, l, UI_C_TXT, UI_C_BG);
    /* decoded buttons held */
    n = snprintf(l, sizeof l, " held:");
    for (c = 0; c < MX_BTN_COUNT; c++)
        if (mx_pressed(mx, (enum mx_btn)c) && n < UI_COLS - 5) { btn_name(nm, sizeof nm, c); n += snprintf(l + n, sizeof l - n, " %s", nm); }
    ui_row((u16)row++, l, UI_C_OK, UI_C_BG);
    row++;
    for (c = 0; c < MX_ENC_COUNT; c++) {
        snprintf(l, sizeof l, " %-8s %s%d   rest %d%d", enc_names[c], plus(enc_count[c]), enc_count[c], mx->enc[c].rest >> 1, mx->enc[c].rest & 1);
        ui_row((u16)row++, l, UI_C_TXT, UI_C_BG);
    }
    row++;
    snprintf(l, sizeof l, " last: %s", last_event);
    ui_row((u16)row++, l, UI_C_DIM, UI_C_BG);
    snprintf(l, sizeof l, " frames %u  drops %u", (unsigned)mx->frames, (unsigned)mx->dropped);
    ui_row((u16)row++, l, UI_C_DIM, UI_C_BG);
    while (row < UI_ROWS) ui_row((u16)row++, "", UI_C_TXT, UI_C_BG);
}
static void input_event(const mx_event *e)
{
    char nm[12];
    if (e->type == MX_EV_ENC) {
        if (e->id < MX_ENC_COUNT) enc_count[e->id] += e->delta;
        snprintf(last_event, sizeof last_event, "%s %s%d", e->id < MX_ENC_COUNT ? enc_names[e->id] : "?", plus(e->delta), e->delta);
    } else {
        btn_name(nm, sizeof nm, e->id);
        snprintf(last_event, sizeof last_event, "%s %s%s", nm,
                 e->type == MX_EV_BTN_DOWN ? "down" : e->type == MX_EV_BTN_HOLD ? "hold" : "up",
                 (e->type == MX_EV_BTN_UP && (e->flags & MX_UP_WAS_HELD)) ? " (held)" : "");
    }
    ui_invalidate();
}
static void input_tick(void) { static u8 k; if (++k >= 2) { k = 0; ui_invalidate(); } }
static const ui_page page_input = { "Input test", input_enter, input_draw, input_event, input_tick };

/* ------------------------------------------------------------------ table */
const ui_page *ui_pages[PAGE_COUNT] = {
    &page_splash, &page_main, &page_routes, &page_log, &page_ble, &page_scan, &page_usb,
    &page_system, &page_display, &page_filters, &ui_page_confirm, &page_input, &page_keyboard,
    &page_route_opt, &page_inputs, &page_buttons, &page_presets, &page_composer, &page_connections,
    &page_stats, &page_recorder, &page_reference, &page_clock,
};
