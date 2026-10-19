/* SPDX-License-Identifier: MIT */
#include "system/includes.h"
#include "app_config.h"
#include "os/os_api.h"
#include "ui.h"
#include "lcd.h"
#include "settings.h"
#include "input/matrix.h"
#include "bt_ble/ble_port.h"
#include "bridge.h"
#include "midi/keys.h"
#include "asm/adc_api.h"
#include "asm/gpio.h"
#include "bt_ble/ble_host.h"
#include "usb/usb_midi.h"
#include "health.h"
#include "fm1_log.h"
#include <stdio.h>

#define LOG_TAG_CONST       APP
#define LOG_TAG             "[UI]"
#define LOG_ERROR_ENABLE
#define LOG_INFO_ENABLE
#include "debug.h"

const u16 UI_C_BG = 0x0000, UI_C_TXT = 0xFFFF, UI_C_DIM = 0x8410, UI_C_OK = 0x07E0, UI_C_WARN = 0xFD20;
const u16 UI_C_TITLE_BG = RGB565(0, 72, 112), UI_C_TITLE_ST = RGB565(160, 200, 224);

extern const ui_page *ui_pages[PAGE_COUNT];     /* ui_pages.c */
extern mx_state *matrix_state(void);
extern u16 bt_ble_is_connected(void);

#define STACK_DEPTH 8                           /* status > menu > System > Presets > slot > confirm is six */
static int stack[STACK_DEPTH]; static int depth;
static u8 need_draw, via_back;
static u32 last_input_ms, last_tick_ms;
static u8 backlight_on = 1;
static u32 ui_popped, ui_last_ev;                  /* for the editor's readout: events popped, last (type << 8 | id) */
void ui_debug(u32 *popped, u32 *last, u32 *bl) { *popped = ui_popped; *last = ui_last_ev; *bl = backlight_on; }
static u8 waking, wake_btn;             /* a button press is waking the panel: its hold and release are swallowed */
static keys_state keys;
static int cur = PAGE_SPLASH;
static u16 bat_mv;
static char toast_text[UI_COLS + 1];
static u32 toast_until;                         /* ms; 0 = none */

keys_state *ui_keys(void) { return &keys; }

void ui_keys_reload(void)                   /* after a settings change: release what is held, re-init */
{
    midi_msg off[KEYS_COUNT];
    fm1_settings *st = settings_get();
    int i, n = keys_release_all(&keys, off, KEYS_COUNT);
    for (i = 0; i < n; i++) bridge_inject(MIDI_PORT_KBD, &off[i]);
    keys_init(&keys, st->kbd_channel, st->kbd_octave, st->kbd_velocity);
}

void ui_octave_shift(int delta)
{
    int o = settings_get()->kbd_octave + delta;
    if (o < -3) o = -3;
    if (o > 3) o = 3;
    if (o == settings_get()->kbd_octave) return;
    settings_get()->kbd_octave = (s8)o;
    settings_mark_dirty();
    ui_keys_reload();
}

/* which settings slot a function button has, -1 if the button belongs to the UI */
static int btn_slot(int id)
{
    switch (id) {
    case MX_BTN_FX: return BTN_SLOT_FX;   case MX_BTN_SCL: return BTN_SLOT_SCL;  case MX_BTN_ENV: return BTN_SLOT_ENV;
    case MX_BTN_LFO: return BTN_SLOT_LFO; case MX_BTN_EDIT: return BTN_SLOT_EDIT; case MX_BTN_SEQ: return BTN_SLOT_SEQ;
    case MX_BTN_ARP: return BTN_SLOT_ARP; case MX_BTN_PLAY: return BTN_SLOT_PLAY; case MX_BTN_REC: return BTN_SLOT_REC;
    default: return -1;
    }
}

/* the 27 note keys and the mapped function buttons work on every page; knobs only
 * on the Keyboard page (KNOB1 is the menu's) */
static int keys_dispatch(const mx_event *e)
{
    midi_msg m;
    mx_state *mx = matrix_state();
    fm1_settings *st = settings_get();
    int is_btn = e->type == MX_EV_BTN_DOWN || e->type == MX_EV_BTN_UP || e->type == MX_EV_BTN_HOLD;
    if (!is_btn) return 0;
    if (e->id >= MX_BTN_KEY0 && e->type == MX_EV_BTN_DOWN) {
        extern int ui_buttons_page_pick(int key);
        if (ui_buttons_page_pick(e->id - MX_BTN_KEY0)) return 1;
    }
    if (e->id >= MX_BTN_KEY0 && cur == PAGE_BUTTONS) return 1;
    /* a function button or a note key with a MIDI job: works even with the keyboard off */
    {
        int slot = btn_slot(e->id);
        const u8 *job = NULL;
        if (slot >= 0 && st->btn[slot].type != BTN_MAP_NONE) job = &st->btn[slot].type;
        if (e->id >= MX_BTN_KEY0 && e->id < MX_BTN_COUNT && st->key[e->id - MX_BTN_KEY0].type != BTN_MAP_NONE) job = &st->key[e->id - MX_BTN_KEY0].type;
        if (job) {
            btn_map map = { job[0], job[1] };
            if (e->type != MX_EV_BTN_HOLD) {
                mx_led_set(mx, (enum mx_btn)e->id, e->type == MX_EV_BTN_DOWN);
                if (keys_button_event(&keys, &map, e->type == MX_EV_BTN_DOWN, &m)) bridge_inject(MIDI_PORT_KBD, &m);
            }
            return 1;
        }
    }
    /* the keyboard proper */
    if (e->id >= MX_BTN_KEY0) {
        int key = e->id - MX_BTN_KEY0;
        if (!st->kbd_enabled || e->type == MX_EV_BTN_HOLD) return 1;   /* keys are never menu buttons */
        mx_led_set(mx, (enum mx_btn)e->id, e->type == MX_EV_BTN_DOWN);
        if (keys_key_event(&keys, key, e->type == MX_EV_BTN_DOWN, &m)) bridge_inject(MIDI_PORT_KBD, &m);
        return 1;
    }
    return 0;
}

/* ---- row cache ---- */
static char shown[UI_ROWS][UI_COLS + 1];
static u16 shown_fg[UI_ROWS], shown_bg[UI_ROWS], shown_fg2[UI_ROWS];
static u8 shown_split[UI_ROWS];
static u8 cache_valid;

void ui_row2(u16 row, const char *text, u16 fg, u16 bg, u16 split, u16 fg2)
{
    char buf[UI_COLS + 1], head[UI_COLS + 1];
    int n = 0;
    if (row >= UI_ROWS) return;
    if (split >= UI_COLS || fg2 == fg) { split = UI_COLS; fg2 = fg; }
    while (n < UI_COLS && text[n]) { buf[n] = text[n]; n++; }
    while (n < UI_COLS) buf[n++] = ' ';
    buf[UI_COLS] = 0;
    if (cache_valid && shown_fg[row] == fg && shown_bg[row] == bg && shown_fg2[row] == fg2 && shown_split[row] == split
        && strcmp(shown[row], buf) == 0) return;
    if (split == UI_COLS) lcd_text(0, row, buf, fg, bg);
    else {
        memcpy(head, buf, split); head[split] = 0;
        lcd_text(0, row, head, fg, bg);
        lcd_text(split, row, buf + split, fg2, bg);
    }
    strcpy(shown[row], buf);
    shown_fg[row] = fg; shown_bg[row] = bg; shown_fg2[row] = fg2; shown_split[row] = (u8)split;
}

void ui_row(u16 row, const char *text, u16 fg, u16 bg) { ui_row2(row, text, fg, bg, UI_COLS, fg); }

void ui_row_inv(u16 row, const char *text) { ui_row(row, text, UI_C_BG, UI_C_TXT); }

void ui_status_short(char *buf, int n)
{
    const char *b = ble_port_ready() ? "ok" : ble_host_active() ? "host" : bt_ble_is_connected() ? "con" : "adv";
    snprintf(buf, n, "U:%s B:%s", usb_midi_configured(0) ? "ok" : "--", b);
}

/* " Title" on the left, the link status on the right, on the title band */
void ui_title(const char *title)
{
    char line[UI_COLS + 1], st[16];
    int n = snprintf(line, sizeof line, " %s", title), sn, pos;
    ui_status_short(st, sizeof st);
    sn = (int)strlen(st);
    pos = UI_COLS - 1 - sn;
    if (pos < n + 1) pos = n + 1;
    while (n < pos && n < UI_COLS) line[n++] = ' ';
    line[n] = 0;
    snprintf(line + n, sizeof line - n, "%s", st);
    ui_row2(0, line, UI_C_TXT, UI_C_TITLE_BG, (u16)pos, UI_C_TITLE_ST);
}

void ui_clear(void)
{
    int r;
    for (r = 0; r < UI_ROWS; r++) ui_row((u16)r, "", UI_C_TXT, UI_C_BG);
}

void ui_invalidate(void) { cache_valid = 0; need_draw = 1; }
void ui_refresh(void) { need_draw = 1; }

/* ---- toast ---- */
void ui_toast(const char *text)
{
    snprintf(toast_text, sizeof toast_text, " %s", text);
    toast_until = timer_get_ms() + 1200;
    if (!toast_until) toast_until = 1;
    need_draw = 1;
}

static void toast_draw(void)
{
    if (toast_until) ui_row(UI_ROWS - 1, toast_text, UI_C_OK, UI_C_BG);
}

/* ---- battery ---- */
/* Thresholds on the divider reading, uncalibrated until a unit is measured (verify on
 * hardware: the stock firmware's own low-battery point is unknown). */
#define BAT_FULL_MV 4000
#define BAT_3_MV    3800
#define BAT_2_MV    3650
#define BAT_LOW_MV  3500

u16 ui_battery_mv(void) { return bat_mv; }
int ui_battery_low(void) { return bat_mv && bat_mv < BAT_LOW_MV; }
void ui_battery_bar(char *buf, int n)
{
    int cells = bat_mv >= BAT_FULL_MV ? 4 : bat_mv >= BAT_3_MV ? 3 : bat_mv >= BAT_2_MV ? 2 : bat_mv >= BAT_LOW_MV ? 1 : 0, i;
    if (n < 7) { if (n) buf[0] = 0; return; }
    buf[0] = '[';
    for (i = 0; i < 4; i++) buf[1 + i] = i < cells ? '#' : '.';
    buf[5] = ']'; buf[6] = 0;
}

/* ---- navigation ---- */
int ui_current(void) { return cur; }
int ui_came_back(void) { return via_back; }

static void enter(int page)
{
    cur = page;
    cache_valid = 0;                       /* a page draws everything it owns */
    lcd_fill(0, 0, LCD_W, LCD_H, UI_C_BG);
    memset(shown, 0, sizeof shown);
    cache_valid = 1;
    if (ui_pages[cur]->enter) ui_pages[cur]->enter();
    via_back = 0;
    need_draw = 1;
}

void ui_goto(int page)
{
    if (page < 0 || page >= PAGE_COUNT || page == cur) return;
    if (depth < STACK_DEPTH) stack[depth++] = cur;
    via_back = 0;
    enter(page);
}

void ui_back(void)
{
    via_back = 1;
    if (depth > 0) enter(stack[--depth]);
    else enter(cur == PAGE_HOME ? PAGE_MAIN : PAGE_HOME);
}

void ui_home(void) { depth = 0; via_back = 0; enter(PAGE_HOME); }

/* ---- confirm page ---- */
static const char *confirm_text;
static void (*confirm_yes)(void);
static u8 confirm_sel;                      /* 0 = no (the default), 1 = yes */
static void confirm_draw(void)
{
    ui_title("Confirm");
    ui_row(3, confirm_text ? confirm_text : "", UI_C_TXT, UI_C_BG);
    if (confirm_sel) ui_row_inv(6, "  yes"); else ui_row(6, "  yes", UI_C_TXT, UI_C_BG);
    if (!confirm_sel) ui_row_inv(7, "  no"); else ui_row(7, "  no", UI_C_TXT, UI_C_BG);
    ui_row(UI_ROWS - 1, "SEL:choose OCT+:ok OCT-:no", UI_C_DIM, UI_C_BG);
}
static void confirm_event(const mx_event *e)
{
    if (e->type == MX_EV_ENC && (e->id == MX_ENC_SELECT || e->id == MX_ENC_PRESETS)) {
        confirm_sel = (u8)(e->delta < 0);          /* yes is the upper row: anticlockwise reaches it */
        ui_invalidate();
        return;
    }
    if (e->type != MX_EV_BTN_UP || (e->flags & MX_UP_WAS_HELD)) return;
    if (e->id == MX_BTN_OCT_PLUS && confirm_sel) { void (*f)(void) = confirm_yes; ui_back(); if (f) f(); }
    else if (e->id == MX_BTN_OCT_PLUS || e->id == MX_BTN_OCT_MINUS) ui_back();
}
static void confirm_enter(void) { confirm_sel = 0; }
const ui_page ui_page_confirm = { "Confirm", confirm_enter, confirm_draw, confirm_event, NULL };

void ui_confirm(const char *text, void (*yes)(void))
{
    confirm_text = text;
    confirm_yes = yes;
    ui_goto(PAGE_CONFIRM);
}

/* ---- backlight ---- */
static u32 backlight_ms(void)
{
    static const u32 t[BACKLIGHT_COUNT] = {0, 10000, 30000, 60000, 300000};
    u8 i = settings_get()->backlight_idx;
    return t[i < BACKLIGHT_COUNT ? i : BACKLIGHT_30S];
}

static void backlight_poll(u32 now)
{
    u32 t = backlight_ms();
    if (backlight_on && t && now - last_input_ms >= t) { backlight_on = 0; lcd_sleep(1); }
}

/* ---- task ---- */
static void dispatch(const mx_event *e)
{
    if (keys_dispatch(e)) {                 /* MIDI jobs first: a note or a page turn works with the panel dark */
        if (!backlight_on) { backlight_on = 1; lcd_sleep(0); ui_invalidate(); }
        return;
    }
    if (!backlight_on) {                    /* any other input only wakes the panel, on the press itself */
        if (e->type == MX_EV_BTN_DOWN) { wake_btn = e->id; waking = 1; }
        if (e->type != MX_EV_BTN_UP || !waking) { backlight_on = 1; lcd_sleep(0); ui_invalidate(); }
        if (e->type == MX_EV_BTN_UP) waking = 0;
        return;
    }
    if (waking) {                           /* the rest of the press that woke the panel: not for the page */
        if (e->type == MX_EV_BTN_UP && e->id == wake_btn) waking = 0;
        if (e->type != MX_EV_ENC && e->id == wake_btn) return;
    }
    if (e->type != MX_EV_ENC && e->id == MX_BTN_HOME && cur != PAGE_SPLASH) {
        if (e->type == MX_EV_BTN_HOLD) { if (cur != PAGE_PERFORM) ui_goto(PAGE_PERFORM); return; }
        if (e->type == MX_EV_BTN_UP && !(e->flags & MX_UP_WAS_HELD)) {
            if (cur == PAGE_HOME) ui_goto(PAGE_MAIN); else ui_home();
            return;
        }
        if (e->type == MX_EV_BTN_UP) return;
    }
    if (e->type == MX_EV_BTN_HOLD && e->id == MX_BTN_GLO) {      /* hold GLO: BLE power saving on/off, from anywhere */
        fm1_settings *st = settings_get();
        st->ble_link_mode = st->ble_link_mode == BLE_LINK_POWER_SAVING ? BLE_LINK_LOW_LATENCY : BLE_LINK_POWER_SAVING;
        settings_mark_dirty();
        ble_port_set_link_mode(st->ble_link_mode);
        ui_toast(st->ble_link_mode == BLE_LINK_POWER_SAVING ? "BLE power saving on" : "BLE low latency");
        ui_invalidate();
        return;
    }
    if (e->type == MX_EV_BTN_UP && e->id == MX_BTN_GLO && (e->flags & MX_UP_WAS_HELD)) return;
    if (ui_pages[cur]->on_event) ui_pages[cur]->on_event(e);
}

/* KNOB1..4: a quick turn steps by 3, a flick by 10 (the gap between clicks as the task
 * sees them; clicks popped in one pass are 0 ms apart). Pages that add the delta get
 * the big steps, pages that only look at its sign do not. */
static void accelerate(mx_event *e, u32 now)
{
    static u32 last[MX_ENC_COUNT];
    u32 gap;
    if (e->type != MX_EV_ENC || e->id < MX_ENC_KNOB1 || e->id > MX_ENC_KNOB4) return;
    gap = now - last[e->id];
    last[e->id] = now;
    if (gap < 40) e->delta = (s8)(e->delta * 10);
    else if (gap < 100) e->delta = (s8)(e->delta * 3);
}

static void ui_task(void *p)
{
    mx_state *mx = matrix_state();
    mx_event e;
    (void)p;
    if (health_get()->unclean >= HEALTH_SAFE_BOOTS)
        fm1_log("safe boot: %u boots in a row never ran clean, the panel is left alone", (unsigned)health_get()->unclean);
    else {
        fm1_log("lcd init %s", lcd_init() == 0 ? "ok" : "FAILED");
        lcd_invert(settings_get()->invert);
    }
    keys_init(&keys, settings_get()->kbd_channel, settings_get()->kbd_octave, settings_get()->kbd_velocity);
    gpio_set_die(IO_PORTB_06, 0); gpio_set_direction(IO_PORTB_06, 1); gpio_set_pull_up(IO_PORTB_06, 0); gpio_set_pull_down(IO_PORTB_06, 0);
    adc_add_sample_ch(AD_CH_PB06);                       /* MASTER pot */
    last_input_ms = timer_get_ms();
    bat_mv = (u16)adc_get_voltage(AD_CH_PB01);
    enter(PAGE_SPLASH);
    for (;;) {
        u32 now = timer_get_ms();
        while (mx_pop(mx, &e)) {
            ui_popped++; ui_last_ev = (u32)e.type << 8 | e.id;
            last_input_ms = now;
            accelerate(&e, now);
            dispatch(&e);
        }
        if (need_draw) {
            need_draw = 0;
            if (ui_pages[cur]->draw) ui_pages[cur]->draw();
            toast_draw();
        }
        if (now - last_tick_ms >= 100) {
            last_tick_ms = now;
            if (toast_until && (s32)(now - toast_until) >= 0) { toast_until = 0; ui_invalidate(); }
            if (ui_pages[cur]->tick) ui_pages[cur]->tick();
#if FM1_BLE_HOST_EN
            ble_host_tick();
#endif
            { static u8 hk; if (++hk >= 10) { hk = 0; health_check(); fm1_log_stack_note(FM1_TASK_UI); bat_mv = (u16)adc_get_voltage(AD_CH_PB01);
                                       if (now > 20000) health_mark_clean(); } }   /* 20 s up: not a crash loop */
            if (settings_get()->kbd_vel_source == 1) {           /* MASTER pot: 0..3300 mV -> 1..127 */
                u32 mv = adc_get_voltage(AD_CH_PB06);
                u32 v = mv * 127u / 3300u;
                keys.velocity = (u8)(v < 1 ? 1 : v > 127 ? 127 : v);
            }
            mx_led_set(mx, MX_BTN_HOME, cur == PAGE_HOME);
            mx_led_set(mx, MX_BTN_GLO, settings_get()->ble_link_mode == BLE_LINK_POWER_SAVING);
            {   /* activity: REC blinks on USB traffic, PLAY on BLE traffic (unless mapped to MIDI) */
                static u32 last_usb, last_ble;
                midi_router *r = bridge_router();
                u32 usb = r->count_in[MIDI_PORT_USB] + r->count_out[MIDI_PORT_USB];
                u32 ble = r->count_in[MIDI_PORT_BLE] + r->count_out[MIDI_PORT_BLE];
                if (settings_get()->btn[BTN_SLOT_REC].type == BTN_MAP_NONE) mx_led_set(mx, MX_BTN_REC, usb != last_usb);
                if (settings_get()->btn[BTN_SLOT_PLAY].type == BTN_MAP_NONE) mx_led_set(mx, MX_BTN_PLAY, ble != last_ble);
                last_usb = usb; last_ble = ble;
            }
        }
        settings_poll();
        backlight_poll(now);
        os_time_dly(1);
    }
}

void ui_init(void)
{
    os_task_create(ui_task, NULL, 8, 1024, 0, "ui");
}
