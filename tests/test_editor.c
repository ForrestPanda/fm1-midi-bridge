/* Host test of the SysEx settings editor: info, get, set, status; and the struct
 * offsets web/editor.html relies on. */
#include "../app/editor.h"
#include "../app/settings.h"
#include "../app/midi/router.h"
#include "../app/update/fm1_update.h"
#include "../app/util/pack7.h"
#include "../app/util/logring.h"
#include <stdio.h>
#include <string.h>
#include <stddef.h>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static uint8_t last[1024]; static uint32_t last_n; static int applied;
int editor_port_send(const uint8_t *wire, uint32_t n) { memcpy(last, wire, n); last_n = n; return 0; }
void editor_port_apply(void) { applied++; }
void editor_port_status(editor_status *st) { st->count_in[0] = 7; st->usb_ok = 1; }
static int sent_port; static uint8_t sent[16]; static uint32_t sent_n;
void editor_port_send_midi(int port, const uint8_t *bytes, uint32_t n) { sent_port = port; memcpy(sent, bytes, n); sent_n = n; }
static logring ring;
const struct logring *editor_port_log(void) { return &ring; }
void editor_port_sysinfo(editor_sysinfo *si) { si->uptime_ms = 1234; si->boots = 5; si->minimal = 1; }
static uint8_t blob[256]; static int blob_len; static uint8_t pblob[5][256]; static int pblob_len[5];   /* presets 11..14, names 16 */
static int pidx(uint16_t id) { return id == FM1_PRESET_NAMES_ID ? 4 : id - 11; }
static int name_writes;
int settings_port_read(uint16_t id, void *buf, uint16_t len) { uint8_t *b = id == FM1_SETTINGS_ID ? blob : pblob[pidx(id)]; int *bl = id == FM1_SETTINGS_ID ? &blob_len : &pblob_len[pidx(id)]; if (!*bl) return -1; if (len > *bl) len = (uint16_t)*bl; memcpy(buf, b, len); return len; }
int settings_port_write(uint16_t id, const void *buf, uint16_t len) { uint8_t *b = id == FM1_SETTINGS_ID ? blob : pblob[pidx(id)]; int *bl = id == FM1_SETTINGS_ID ? &blob_len : &pblob_len[pidx(id)]; if (id == FM1_PRESET_NAMES_ID) name_writes++; memcpy(b, buf, len); *bl = len; return len; }
uint32_t settings_port_now_ms(void) { return 0; }

static uint32_t frame(uint8_t *out, uint8_t cmd, const uint8_t *payload, uint32_t n, int packed)
{
    uint32_t w = 0;
    out[w++] = 0xF0; out[w++] = 0x7D; out[w++] = 'F'; out[w++] = 'M'; out[w++] = '1'; out[w++] = cmd;
    if (payload) { if (packed) w += pack7(payload, n, out + w); else { memcpy(out + w, payload, n); w += n; } }
    out[w++] = 0xF7;
    return w;
}

int main(void)
{
    uint8_t f[1024], dec[256]; uint32_t n, d;
    fm1_settings s;
    /* offsets the web page uses */
    CHECK(sizeof(fm1_settings) == 212);
    CHECK(offsetof(fm1_settings, routes) == 6 && offsetof(fm1_settings, filters) == 8 && offsetof(fm1_settings, usb_enabled) == 9);
    CHECK(offsetof(fm1_settings, backlight_idx) == 10 && offsetof(fm1_settings, invert) == 11 && offsetof(fm1_settings, ble_mode) == 12);
    CHECK(offsetof(fm1_settings, ble_host_pair) == 13 && offsetof(fm1_settings, host_peer) == 14 && offsetof(fm1_settings, ble_link_mode) == 21);
    CHECK(offsetof(fm1_settings, kbd_enabled) == 22 && offsetof(fm1_settings, kbd_channel) == 23 && offsetof(fm1_settings, kbd_octave) == 24 && offsetof(fm1_settings, kbd_velocity) == 25);
    CHECK(offsetof(fm1_settings, route) == 26 && offsetof(fm1_settings, input) == 90 && offsetof(fm1_settings, btn) == 106 && offsetof(fm1_settings, key) == 126 && offsetof(fm1_settings, fav) == 182 && offsetof(fm1_settings, clock_bpm) == 202 && offsetof(fm1_settings, log_hide) == 208 && offsetof(fm1_settings, crc) == 210 && offsetof(fm1_settings, crc) == sizeof(fm1_settings) - 2);

    settings_init();
    n = frame(f, 0x01, NULL, 0, 0);
    CHECK(editor_is_frame(f, n) && editor_handle(f, n) == 1);
    CHECK(last[5] == 0x41 && memcmp(last + 6, UPD_IDENTITY, strlen(UPD_IDENTITY)) == 0);
    /* get */
    n = frame(f, 0x02, NULL, 0, 0); editor_handle(f, n);
    CHECK(last[5] == 0x42);
    d = unpack7(last + 6, last_n - 7, dec, sizeof dec);
    CHECK(d == sizeof s); memcpy(&s, dec, sizeof s);
    CHECK(s.magic == FM1_SETTINGS_MAGIC && s.routes == ROUTE_DEFAULT);
    /* set: change routes and a button mapping */
    s.routes = ROUTE_TRS_USB; s.btn[BTN_SLOT_FX].type = 1; s.btn[BTN_SLOT_FX].number = 42; s.host_peer[0] = 9;
    n = frame(f, 0x03, (const uint8_t *)&s, sizeof s, 1); editor_handle(f, n);
    CHECK(last[5] == 0x43 && last[6] == 0 && applied == 1);
    CHECK(settings_get()->routes == ROUTE_TRS_USB && settings_get()->btn[BTN_SLOT_FX].number == 42);
    CHECK(settings_get()->host_peer[0] == 0);                 /* device-owned, not overwritten */
    CHECK(settings_dirty());
    /* set: wrong version refused */
    s.version = 99; n = frame(f, 0x03, (const uint8_t *)&s, sizeof s, 1); editor_handle(f, n);
    CHECK(last[6] == 3 && applied == 1);
    /* set: short blob refused */
    n = frame(f, 0x03, (const uint8_t *)&s, 10, 1); editor_handle(f, n);
    CHECK(last[6] == 1);
    /* status */
    n = frame(f, 0x04, NULL, 0, 0); editor_handle(f, n);
    CHECK(last[5] == 0x44);
    d = unpack7(last + 6, last_n - 7, dec, sizeof dec);
    CHECK(d == sizeof(editor_status) && dec[0] == 7 && dec[offsetof(editor_status, usb_ok)] == 1);
    /* presets */
    n = frame(f, 0x05, NULL, 0, 0); editor_handle(f, n); CHECK(last[5] == 0x45 && last[6] == 0 && last_n == 7 + 4);
    { uint8_t slot = 1; n = frame(f, 0x06, &slot, 1, 0); editor_handle(f, n); CHECK(last[5] == 0x46 && last[6] == 0);
      n = frame(f, 0x05, NULL, 0, 0); editor_handle(f, n); CHECK(last[7] == 1);
      settings_get()->routes = 0; n = frame(f, 0x07, &slot, 1, 0); editor_handle(f, n); CHECK(last[6] == 0 && settings_get()->routes == ROUTE_TRS_USB && applied == 2);
      slot = 9; n = frame(f, 0x07, &slot, 1, 0); editor_handle(f, n); CHECK(last[6] == 1); }
    /* preset names: none at first, set (unprintable bytes dropped, cut at 12, trailing spaces trimmed),
     * the same name again writes nothing, kept across a reload, a save keeps it, a clear drops it */
    n = frame(f, 0x0E, NULL, 0, 0); editor_handle(f, n);
    CHECK(last[5] == 0x4E && last[6] == 0 && last_n == 8 + 4 * FM1_PRESET_NAME_LEN && last[7] == 0);
    { uint8_t slot, pl[20] = { 2, 'L', 'i', 'v', 'e', 0x01, ' ', 's', 'e', 't', ' ', 'B', '-', 'x', 'y', 'z' };
      n = frame(f, 0x0E, pl, 16, 0); editor_handle(f, n);
      CHECK(last[6] == 0 && memcmp(last + 7 + 2 * FM1_PRESET_NAME_LEN, "Live set B-x", 12) == 0 && name_writes == 1);
      n = frame(f, 0x0E, pl, 16, 0); editor_handle(f, n); CHECK(name_writes == 1);
      memcpy(pl, (uint8_t[]){ 1, 'G', 'i', 'g', ' ', ' ' }, 6); n = frame(f, 0x0E, pl, 6, 0); editor_handle(f, n);
      CHECK(strcmp(settings_preset_name(1), "Gig") == 0);
      pl[0] = 7; n = frame(f, 0x0E, pl, 2, 0); editor_handle(f, n); CHECK(last[6] == 1);
      settings_init(); CHECK(strcmp(settings_preset_name(1), "Gig") == 0 && strcmp(settings_preset_name(2), "Live set B-x") == 0);
      slot = 1; n = frame(f, 0x06, &slot, 1, 0); editor_handle(f, n); CHECK(strcmp(settings_preset_name(1), "Gig") == 0);
      n = frame(f, 0x08, &slot, 1, 0); editor_handle(f, n); CHECK(settings_preset_name(1)[0] == 0 && strcmp(settings_preset_name(2), "Live set B-x") == 0);
      pl[0] = 2; n = frame(f, 0x0E, pl, 1, 0); editor_handle(f, n); CHECK(last[6] == 0 && settings_preset_name(2)[0] == 0); }
    /* send raw MIDI to a port */
    { uint8_t pl[1 + 8]; uint8_t midi[3] = {0x90, 60, 100}; pl[0] = 3; uint32_t k = pack7(midi, 3, pl + 1);
      n = frame(f, 0x09, pl, 1 + k, 0); editor_handle(f, n);
      CHECK(last[5] == 0x49 && last[6] == 0 && sent_port == 3 && sent_n == 3 && sent[0] == 0x90 && sent[2] == 100);
      pl[0] = 7; n = frame(f, 0x09, pl, 1 + k, 0); editor_handle(f, n); CHECK(last[6] == 1); }
    /* log ring: offsets are absolute, the oldest bytes fall out, reads clamp */
    { uint8_t o[16]; uint32_t from, k, i; char line[8];
      logring_init(&ring);
      CHECK(logring_total(&ring) == 0 && logring_start(&ring) == 0);
      from = 0; CHECK(logring_read(&ring, &from, o, sizeof o) == 0);
      logring_write(&ring, "hello\n", 6);
      from = 0; k = logring_read(&ring, &from, o, sizeof o); CHECK(k == 6 && from == 0 && memcmp(o, "hello\n", 6) == 0);
      from = 4; k = logring_read(&ring, &from, o, sizeof o); CHECK(k == 2 && o[0] == 'o');
      from = 99; k = logring_read(&ring, &from, o, sizeof o); CHECK(k == 0 && from == 6);
      for (i = 0; i < LOGRING_SIZE; i++) { line[0] = (char)('a' + i % 26); logring_write(&ring, line, 1); }
      CHECK(logring_total(&ring) == LOGRING_SIZE + 6 && logring_start(&ring) == 6);
      from = 0; k = logring_read(&ring, &from, o, 3); CHECK(k == 3 && from == 6 && o[0] == 'a' && o[1] == 'b');
      from = LOGRING_SIZE + 4; k = logring_read(&ring, &from, o, 16); CHECK(k == 2 && o[0] == (uint8_t)('a' + (LOGRING_SIZE - 2) % 26)); }
    /* 0x0A: log from an offset; reply carries total, from, bytes */
    { uint8_t pl[4] = { 8, 0, 0, 0 }; uint32_t t, fr;
      n = frame(f, 0x0A, pl, 4, 0); editor_handle(f, n);
      CHECK(last[5] == 0x4A);
      d = unpack7(last + 6, last_n - 7, dec, sizeof dec);
      t = dec[0] | dec[1] << 8 | dec[2] << 16 | (uint32_t)dec[3] << 24; fr = dec[4] | dec[5] << 8 | dec[6] << 16 | (uint32_t)dec[7] << 24;
      CHECK(t == LOGRING_SIZE + 6 && fr == 8 && d == 8 + EDITOR_LOG_CHUNK && dec[8] == 'c');
      pl[0] = 0x7F; pl[1] = 0x7F; pl[2] = 0x7F; pl[3] = 0x7F;                 /* far past the end: nothing, from = total */
      n = frame(f, 0x0A, pl, 4, 0); editor_handle(f, n);
      d = unpack7(last + 6, last_n - 7, dec, sizeof dec);
      fr = dec[4] | dec[5] << 8 | dec[6] << 16 | (uint32_t)dec[7] << 24; CHECK(d == 8 && fr == LOGRING_SIZE + 6);
      n = frame(f, 0x0A, NULL, 0, 0); editor_handle(f, n);                      /* no offset: from the oldest held */
      d = unpack7(last + 6, last_n - 7, dec, sizeof dec);
      fr = dec[4] | dec[5] << 8 | dec[6] << 16 | (uint32_t)dec[7] << 24; CHECK(fr == 6 && dec[8] == 'a'); }
    /* 0x0B: sysinfo */
    n = frame(f, 0x0B, NULL, 0, 0); editor_handle(f, n);
    CHECK(last[5] == 0x4B);
    d = unpack7(last + 6, last_n - 7, dec, sizeof dec);
    CHECK(d == sizeof(editor_sysinfo) && dec[0] == (1234 & 0xFF) && dec[offsetof(editor_sysinfo, boots)] == 5 && dec[offsetof(editor_sysinfo, minimal)] == 1);
    /* 0x0D: host devices (the weak default: state 0, none) */
    n = frame(f, 0x0D, NULL, 0, 0); editor_handle(f, n);
    CHECK(last[5] == 0x4D);
    d = unpack7(last + 6, last_n - 7, dec, sizeof dec);
    CHECK(d == 2 && dec[0] == 0 && dec[1] == 0);
    /* not ours */
    { uint8_t other[] = {0xF0, 0x7E, 0x7F, 0x09, 0x01, 0xF7}; CHECK(!editor_is_frame(other, sizeof other)); }
    if (fails) { printf("%d failure(s)\n", fails); return 1; }
    printf("editor tests passed\n");
    return 0;
}
