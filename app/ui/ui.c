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
#include "health.h"

#define LOG_TAG_CONST       APP
#define LOG_TAG             "[UI]"
#define LOG_ERROR_ENABLE
#define LOG_INFO_ENABLE
#include "debug.h"

const u16 UI_C_BG = 0x0000, UI_C_TXT = 0xFFFF, UI_C_DIM = 0x8410, UI_C_OK = 0x07E0, UI_C_WARN = 0xFD20;

extern const ui_page *ui_pages[PAGE_COUNT];     /* ui_pages.c */
extern mx_state *matrix_state(void);

static int stack[4]; static int depth;
static u8 need_draw;
static u32 last_input_ms, last_tick_ms;
static u8 backlight_on = 1;
static keys_state keys;
static int cur = PAGE_SPLASH;

keys_state *ui_keys(void) { return &keys; }

void ui_keys_reload(void)                   /* after a settings change: release what is held, re-init */
{
    midi_msg off[KEYS_COUNT];
    fm1_settings *st = settings_get();
    int i, n = keys_release_all(&keys, off, KEYS_COUNT);
    for (i = 0; i < n; i++) bridge_inject(MIDI_PORT_KBD, &off[i]);
    keys_init(&keys, st->kbd_channel, st->kbd_octave, st->kbd_velocity);
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
static u16 shown_fg[UI_ROWS], shown_bg[UI_ROWS];
static u8 cache_valid;

void ui_row(u16 row, const char *text, u16 fg, u16 bg)
{
    char buf[UI_COLS + 1];
    int n = 0;
    if (row >= UI_ROWS) return;
    while (n < UI_COLS && text[n]) { buf[n] = text[n]; n++; }
    while (n < UI_COLS) buf[n++] = ' ';
    buf[UI_COLS] = 0;
    if (cache_valid && shown_fg[row] == fg && shown_bg[row] == bg && strcmp(shown[row], buf) == 0) return;
    lcd_text(0, row, buf, fg, bg);
    strcpy(shown[row], buf);
    shown_fg[row] = fg; shown_bg[row] = bg;
}

void ui_row_inv(u16 row, const char *text) { ui_row(row, text, UI_C_BG, UI_C_TXT); }

void ui_clear(void)
{
    int r;
    for (r = 0; r < UI_ROWS; r++) ui_row((u16)r, "", UI_C_TXT, UI_C_BG);
}

void ui_invalidate(void) { cache_valid = 0; need_draw = 1; }

/* ---- navigation ---- */
int ui_current(void) { return cur; }

static void enter(int page)
{
    cur = page;
    cache_valid = 0;                       /* a page draws everything it owns */
    lcd_fill(0, 0, LCD_W, LCD_H, UI_C_BG);
    memset(shown, 0, sizeof shown);
    cache_valid = 1;
    if (ui_pages[cur]->enter) ui_pages[cur]->enter();
    need_draw = 1;
}

void ui_goto(int page)
{
    if (page < 0 || page >= PAGE_COUNT) return;
    if (depth < 4) stack[depth++] = cur;
    enter(page);
}

void ui_back(void)
{
    if (depth > 0) enter(stack[--depth]);
    else enter(PAGE_MAIN);
}

void ui_home(void) { depth = 0; enter(PAGE_MAIN); }

/* ---- confirm page ---- */
static const char *confirm_text;
static void (*confirm_yes)(void);
static void confirm_draw(void)
{
    ui_row_inv(0, " Confirm");
    ui_row(3, confirm_text ? confirm_text : "", UI_C_TXT, UI_C_BG);
    ui_row(6, "  OCT+  yes", UI_C_OK, UI_C_BG);
    ui_row(7, "  OCT-  no", UI_C_DIM, UI_C_BG);
}
static void confirm_event(const mx_event *e)
{
    if (e->type != MX_EV_BTN_UP || (e->flags & MX_UP_WAS_HELD)) return;
    if (e->id == MX_BTN_OCT_PLUS) { void (*f)(void) = confirm_yes; ui_back(); if (f) f(); }
    else if (e->id == MX_BTN_OCT_MINUS) ui_back();
}
const ui_page ui_page_confirm = { "Confirm", NULL, confirm_draw, confirm_event, NULL };

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
    if (backlight_on && t && now - last_input_ms >= t) { backlight_on = 0; lcd_backlight(0); }
}

/* ---- task ---- */
static void dispatch(const mx_event *e)
{
    if (!backlight_on) {                    /* any input only wakes the panel */
        if (e->type == MX_EV_BTN_UP || e->type == MX_EV_ENC) { backlight_on = 1; lcd_backlight(1); }
        return;
    }
    if (keys_dispatch(e)) return;
    if (e->type == MX_EV_BTN_UP && e->id == MX_BTN_HOME && !(e->flags & MX_UP_WAS_HELD) && cur != PAGE_SPLASH) {
        ui_home();
        return;
    }
    if (e->type == MX_EV_BTN_HOLD && e->id == MX_BTN_GLO) {      /* hold GLO: BLE power saving on/off, from anywhere */
        fm1_settings *st = settings_get();
        st->ble_link_mode = st->ble_link_mode == BLE_LINK_POWER_SAVING ? BLE_LINK_LOW_LATENCY : BLE_LINK_POWER_SAVING;
        settings_mark_dirty();
        ble_port_set_link_mode(st->ble_link_mode);
        ui_invalidate();
        return;
    }
    if (e->type == MX_EV_BTN_UP && e->id == MX_BTN_GLO && (e->flags & MX_UP_WAS_HELD)) return;
    if (ui_pages[cur]->on_event) ui_pages[cur]->on_event(e);
}

static void ui_task(void *p)
{
    mx_state *mx = matrix_state();
    mx_event e;
    (void)p;
    lcd_init();
    lcd_invert(settings_get()->invert);
    keys_init(&keys, settings_get()->kbd_channel, settings_get()->kbd_octave, settings_get()->kbd_velocity);
    gpio_set_die(IO_PORTB_06, 0); gpio_set_direction(IO_PORTB_06, 1); gpio_set_pull_up(IO_PORTB_06, 0); gpio_set_pull_down(IO_PORTB_06, 0);
    adc_add_sample_ch(AD_CH_PB06);                       /* MASTER pot */
    last_input_ms = timer_get_ms();
    enter(PAGE_SPLASH);
    for (;;) {
        u32 now = timer_get_ms();
        while (mx_pop(mx, &e)) {
            last_input_ms = now;
            dispatch(&e);
        }
        if (need_draw) {
            need_draw = 0;
            if (ui_pages[cur]->draw) ui_pages[cur]->draw();
        }
        if (now - last_tick_ms >= 100) {
            last_tick_ms = now;
            if (ui_pages[cur]->tick) ui_pages[cur]->tick();
#if FM1_BLE_HOST_EN
            ble_host_tick();
#endif
            { static u8 hk; if (++hk >= 10) { hk = 0; health_check(); } }
            if (settings_get()->kbd_vel_source == 1) {           /* MASTER pot: 0..3300 mV -> 1..127 */
                u32 mv = adc_get_voltage(AD_CH_PB06);
                u32 v = mv * 127u / 3300u;
                keys.velocity = (u8)(v < 1 ? 1 : v > 127 ? 127 : v);
            }
            mx_led_set(mx, MX_BTN_HOME, cur == PAGE_MAIN);
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
