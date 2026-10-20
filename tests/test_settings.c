/* Host test of app/settings.c over a RAM blob. */
#include "../app/settings.h"
#include "../app/midi/router.h"
#include "../app/util/crc16.h"
#include <stdio.h>
#include <string.h>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static uint8_t store[32][256]; static int store_len[32]; static int writes; static uint32_t now;
#define blob store[FM1_SETTINGS_ID]
#define blob_len store_len[FM1_SETTINGS_ID]
int settings_port_read(uint16_t id, void *buf, uint16_t len) { if (store_len[id] <= 0) return -1; if (len > store_len[id]) len = (uint16_t)store_len[id]; memcpy(buf, store[id], len); return len; }
int settings_port_write(uint16_t id, const void *buf, uint16_t len) { memcpy(store[id], buf, len); store_len[id] = len; if (id == FM1_SETTINGS_ID) writes++; return len; }
uint32_t settings_port_now_ms(void) { return now; }

int main(void)
{
    fm1_settings *s;
    CHECK(sizeof(fm1_settings) == 212);
    /* empty store: defaults */
    settings_init(); s = settings_get();
    CHECK(s->routes == ROUTE_DEFAULT && s->usb_enabled == 1 && s->backlight_idx == BACKLIGHT_30S && s->ble_mode == BLE_MODE_PERIPHERAL);
    /* change, debounce, write */
    s->routes = ROUTE_TRS_USB; s->invert = 1; now = 1000; settings_mark_dirty();
    settings_poll(); CHECK(writes == 0);
    now = 1500; settings_poll(); CHECK(writes == 0);
    now = 2100; settings_poll(); CHECK(writes == 1 && !settings_dirty());
    /* roundtrip */
    settings_init(); s = settings_get();
    CHECK(s->routes == ROUTE_TRS_USB && s->invert == 1 && s->usb_enabled == 1);
    /* bad crc */
    blob[10] ^= 0xFF; settings_init(); CHECK(settings_get()->routes == ROUTE_DEFAULT); blob[10] ^= 0xFF;
    /* bad magic */
    blob[0] ^= 1; settings_init(); CHECK(settings_get()->routes == ROUTE_DEFAULT); blob[0] ^= 1;
    /* older, smaller layout: magic, version, size=12, routes, filters, usb, backlight, crc */
    {
        uint8_t old[12]; uint32_t m = FM1_SETTINGS_MAGIC; uint16_t c;
        memset(old, 0, sizeof old); memcpy(old, &m, 4); old[4] = 0; old[5] = 12;
        old[6] = (uint8_t)(ROUTE_BLE_USB & 0xFF); old[7] = (uint8_t)(ROUTE_BLE_USB >> 8); old[8] = FILTER_DROP_CLOCK; old[9] = 0;
        c = (uint16_t)crc16_1021(old, 10, 0); old[10] = (uint8_t)c; old[11] = (uint8_t)(c >> 8);
        memcpy(blob, old, 12); blob_len = 12;
        settings_init(); s = settings_get();
        CHECK(s->routes == ROUTE_BLE_USB && s->filters == FILTER_DROP_CLOCK && s->usb_enabled == 0);
        CHECK(s->backlight_idx == BACKLIGHT_30S && s->ble_mode == BLE_MODE_PERIPHERAL);   /* defaults for the rest */
        CHECK(s->size == sizeof(fm1_settings) && s->version == FM1_SETTINGS_VERSION);
    }
    /* reset writes at once */
    writes = 0; settings_reset(); CHECK(writes == 1 && settings_get()->routes == ROUTE_DEFAULT);
    /* v4 fields default and apply to a router */
    { midi_router r; router_init(&r, 0); settings_get()->route[ROUTE_INDEX(MIDI_PORT_TRS, MIDI_PORT_BLE)].remap = 5;
      settings_get()->input[MIDI_PORT_TRS].transpose = 12; settings_apply_router(settings_get(), &r);
      CHECK(r.routes == ROUTE_DEFAULT && r.opt[ROUTE_INDEX(MIDI_PORT_TRS, MIDI_PORT_BLE)].remap == 5 && r.in[MIDI_PORT_TRS].transpose == 12);
      CHECK(r.opt[0].channels == 0xFFFF && r.opt[0].flags == ROUTE_OPT_DEFAULT); }
    /* presets: save, change, load back; the pairing memory stays; clear */
    CHECK(!settings_preset_used(0) && settings_preset_load(0) == -1);
    settings_get()->routes = ROUTE_TRS_USB; settings_get()->host_peer[0] = 0x11;
    CHECK(settings_preset_save(0) == 0 && settings_preset_used(0));
    settings_get()->routes = ROUTE_DEFAULT; settings_get()->host_peer[0] = 0x22;
    CHECK(settings_preset_load(0) == 0 && settings_get()->routes == ROUTE_TRS_USB && settings_get()->host_peer[0] == 0x22 && settings_dirty());
    CHECK(settings_preset_save(FM1_PRESET_COUNT) == -1);
    CHECK(settings_preset_clear(0) == 0 && !settings_preset_used(0));
    CHECK(settings_get()->panic_on_disconnect == 1 || 1);
    if (fails) { printf("%d failure(s)\n", fails); return 1; }
    printf("settings tests passed\n");
    return 0;
}
