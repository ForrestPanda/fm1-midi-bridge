/* SPDX-License-Identifier: MIT */
/* TRS tester for the FM-1: an STM32F4-Discovery (168 MHz, the TRS MIDI bridge project's board code)
 * that is a USB-MIDI device ("FM-1 TRS Tester") and plays a byte script from the PC on its MIDI OUT,
 * USART2 TX on PA2 at 31250 baud, timed by TIM2 at 1 MHz. tools/trs_test.py writes the scripts and
 * checks what the FM-1 forwards to USB and BLE.
 *
 * Wiring to the FM-1's MIDI IN (Type A): tip (sink) to PA2, ring (source) to PA3, which the tester
 * drives high; the FM-1's input has its own series resistors (a 10 ohm on PA2 and 33 ohm on PA3 are
 * kinder to the pins while a plug goes in). Sleeve to GND or open: the input is an optocoupler.
 * PD0/PD1 (the mask-ROM key's lines on the dongle) are left as inputs.
 *
 * Commands, SysEx F0 7D 46 54 CMD [pack7 payload] F7 (pack7: per 7 bytes one byte of their top bits,
 * bit i for byte i, then the 7 bytes' low bits); replies the same with CMD | 0x40:
 *   01 clear the script                       -> 41
 *   02 + records: append to the script        -> 42 + u32 bytes held
 *   03 play                                   -> 43 at the start, 50 + stats at the end
 *   04 stop                                   -> 44 + stats
 *   05 status                                 -> 45 + stats
 *   06 ping                                   -> 46 + "FM1TRS 1"
 * A record is u32 at (us from the start of play, little endian), u8 len, then len bytes to send;
 * len 0 is an action, u8 op + u16 arg: op 1 a line break of arg us, op 2 the baud offset to arg as
 * a signed 0.01 % (0: nominal). Times are when a record is due; bytes queue behind the line, so a
 * record due before the previous one is out goes back to back. Stats: u32 records, bytes,
 * max_queued (bytes waiting when a record was due), max_late (us after due that it was queued).
 * Everything else from USB goes straight to the TRS line (a bridge, for ad hoc bytes).
 * LEDs: green USB up, blue playing, orange bytes going out, red a record queued over 2 ms late. */
#include <string.h>
#include "board.h"
#include "stm32f4xx.h"
#include "tester_uart.h"
#include "tusb.h"

#define SCRIPT_SIZE (96u * 1024u)
#define SX_MAX      1100u               /* one command's SysEx, F0 .. F7 */

static uint8_t  script[SCRIPT_SIZE];
static uint32_t script_len, play_pos, play_t0;
static uint8_t  playing;
static uint32_t st_records, st_bytes, st_max_queued, st_max_late;
static uint32_t orange_off;

static const uint8_t PREFIX[4] = { 0xF0, 0x7D, 0x46, 0x54 };

void OTG_FS_IRQHandler(void) { tud_int_handler(BOARD_TUD_RHPORT); }
uint32_t tusb_time_millis_api(void) { return board_millis(); }
void _init(void) {}

static void timer_init(void)            /* TIM2: 32 bits, 84 MHz / 84 = 1 MHz */
{
    RCC->APB1ENR |= RCC_APB1ENR_TIM2EN;
    __DSB();
    TIM2->PSC = 83;
    TIM2->ARR = 0xFFFFFFFFu;
    TIM2->EGR = TIM_EGR_UG;
    TIM2->CR1 = TIM_CR1_CEN;
}

/* ------------------------------------------------------------------ replies */

static uint32_t pack7(const uint8_t *in, uint32_t n, uint8_t *out)
{
    uint32_t i, o = 0;
    for (i = 0; i < n; i += 7) {
        uint32_t k, m = n - i < 7 ? n - i : 7;
        uint8_t hi = 0;
        for (k = 0; k < m; k++) hi |= (uint8_t)((in[i + k] >> 7) << k);
        out[o++] = hi;
        for (k = 0; k < m; k++) out[o++] = in[i + k] & 0x7F;
    }
    return o;
}

static uint32_t unpack7(const uint8_t *in, uint32_t n, uint8_t *out)
{
    uint32_t i = 0, o = 0;
    while (i < n) {
        uint8_t hi = in[i++];
        uint32_t k;
        for (k = 0; k < 7 && i < n; k++) out[o++] = (uint8_t)(in[i++] | (((hi >> k) & 1u) << 7));
    }
    return o;
}

static void reply(uint8_t cmd, const uint8_t *payload, uint32_t n)
{
    uint8_t msg[6 + 40];
    uint32_t len = 0;
    memcpy(msg, PREFIX, 4); len = 4;
    msg[len++] = cmd;
    len += pack7(payload, n, msg + len);
    msg[len++] = 0xF7;
    tud_midi_stream_write(0, msg, len);
}

static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }

static void reply_stats(uint8_t cmd)
{
    uint8_t s[16];
    put32(s, st_records); put32(s + 4, st_bytes); put32(s + 8, st_max_queued); put32(s + 12, st_max_late);
    reply(cmd, s, sizeof s);
}

/* ------------------------------------------------------------------ the player */

static void play_stop(void)
{
    playing = 0;
    tuart_baud(0);
    board_led_write(LED_BLUE, false);
}

/* one pass: every record that is due goes to the line; a record that does not fit waits */
static void play_task(void)
{
    while (playing) {
        uint32_t now = TIM2->CNT - play_t0, at, len, late;
        const uint8_t *r = script + play_pos;
        if (play_pos >= script_len) {               /* the last bytes on the line: done */
            if (!tuart_idle()) return;
            play_stop();
            reply_stats(0x50);
            return;
        }
        at = r[0] | r[1] << 8 | r[2] << 16 | (uint32_t)r[3] << 24;
        len = r[4];
        if ((int32_t)(now - at) < 0) return;
        if (len == 0) {                             /* an action: on an idle line */
            uint32_t arg = r[6] | r[7] << 8;
            if (!tuart_idle()) return;
            if (r[5] == 1) tuart_break_us(arg);
            else if (r[5] == 2) tuart_baud((int16_t)arg);
            play_pos += 8;
            continue;
        }
        if (tuart_free() < len) return;
        late = now - at;
        if (tuart_queued() > st_max_queued) st_max_queued = tuart_queued();
        if (late > st_max_late) st_max_late = late;
        if (late > 2000) board_led_write(LED_RED, true);
        tuart_write(r + 5, len);
        st_records++;
        st_bytes += len;
        play_pos += 5 + len;
        board_led_write(LED_ORANGE, true);
        orange_off = board_millis() + 30;
    }
}

/* ------------------------------------------------------------------ commands */

static void command(const uint8_t *sx, uint32_t n)  /* F0 7D 46 54 CMD payload F7 */
{
    static uint8_t raw[SX_MAX];
    uint8_t cmd = sx[4], s[4];
    uint32_t m = unpack7(sx + 5, n - 6, raw);
    switch (cmd) {
    case 0x01:
        play_stop(); script_len = 0; reply(0x41, NULL, 0);
        break;
    case 0x02:
        if (!playing && script_len + m <= SCRIPT_SIZE) { memcpy(script + script_len, raw, m); script_len += m; }
        put32(s, script_len); reply(0x42, s, 4);
        break;
    case 0x03:
        st_records = st_bytes = st_max_queued = st_max_late = 0;
        board_led_write(LED_RED, false);
        play_pos = 0; play_t0 = TIM2->CNT; playing = 1;
        board_led_write(LED_BLUE, true);
        reply(0x43, NULL, 0);
        break;
    case 0x04:
        play_stop(); reply_stats(0x44);
        break;
    case 0x05:
        reply_stats(0x45);
        break;
    case 0x06:
        reply(0x46, (const uint8_t *)"FM1TRS 1", 8);
        break;
    }
}

/* USB -> commands or the TRS line. Bytes are read only when the line has room for them, so a host
 * that sends faster than 31250 baud is held back, not dropped. */
static void usb_task(void)
{
    static uint8_t sx[SX_MAX];
    static uint32_t sx_len;
    static uint8_t in_sx, ours;
    uint8_t buf[48];
    uint32_t n, i;
    while (tuart_free() >= sizeof buf && tud_midi_available()) {
        n = tud_midi_stream_read(buf, sizeof buf);
        if (!n) break;
        for (i = 0; i < n; i++) {
            uint8_t b = buf[i];
            if (b == 0xF0) { in_sx = 1; ours = 1; sx_len = 0; }
            if (!in_sx || b >= 0xF8) { if (!playing) tuart_write(&b, 1); continue; }
            if (ours) {
                if (sx_len < SX_MAX) sx[sx_len++] = b;
                if (sx_len <= 4 && sx[sx_len - 1] != PREFIX[sx_len - 1]) {   /* someone else's SysEx: to the line */
                    ours = 0;
                    if (!playing) tuart_write(sx, sx_len);
                }
            } else if (!playing) {
                tuart_write(&b, 1);
            }
            if (b == 0xF7 || (b & 0x80 && b != 0xF0)) {                  /* the end, or a status that ends it */
                if (ours && b == 0xF7 && sx_len >= 6 && sx_len < SX_MAX) command(sx, sx_len);
                in_sx = 0;
            }
        }
    }
}

int main(void)
{
    board_init();
    timer_init();
    tuart_init();
    tud_configure_dwc2_t dwc2_cfg = CFG_TUD_CONFIGURE_DWC2_DEFAULT;
    dwc2_cfg.vbus_sensing = true;
    tud_configure(BOARD_TUD_RHPORT, TUD_CFGID_DWC2, &dwc2_cfg);
    const tusb_rhport_init_t dev_init = { .role = TUSB_ROLE_DEVICE, .speed = TUSB_SPEED_FULL };
    tusb_init(BOARD_TUD_RHPORT, &dev_init);
    for (;;) {
        tud_task();
        usb_task();
        play_task();
        if (orange_off && (int32_t)(board_millis() - orange_off) >= 0) { board_led_write(LED_ORANGE, false); orange_off = 0; }
        board_led_write(LED_GREEN, tud_mounted());
    }
}
