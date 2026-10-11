#include "../app/bt_ble/adv_parse.h"
#include <stdio.h>
#include <string.h>
static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)
int main(void)
{
    uint8_t ad[31]; char name[21]; int n = 0;
    /* flags + complete 128-bit list with the MIDI UUID + shortened name */
    ad[n++] = 2; ad[n++] = 0x01; ad[n++] = 0x06;
    ad[n++] = 17; ad[n++] = 0x07; memcpy(ad + n, midi_service_uuid_le, 16); n += 16;
    ad[n++] = 6; ad[n++] = 0x08; memcpy(ad + n, "Keys!", 5); n += 5;
    CHECK(adv_has_midi_service(ad, (uint8_t)n));
    CHECK(adv_get_name(ad, (uint8_t)n, name, sizeof name) == 5 && strcmp(name, "Keys!") == 0);
    /* another uuid: no */
    ad[5] ^= 0xFF; CHECK(!adv_has_midi_service(ad, (uint8_t)n)); ad[5] ^= 0xFF;
    /* malformed length does not read past the end */
    ad[3] = 40; CHECK(!adv_has_midi_service(ad, (uint8_t)n)); ad[3] = 17;
    /* complete name wins over shortened; long names are truncated */
    { uint8_t b[40]; int m = 0; b[m++] = 4; b[m++] = 0x08; memcpy(b + m, "abc", 3); m += 3;
      b[m++] = 26; b[m++] = 0x09; memcpy(b + m, "ABCDEFGHIJKLMNOPQRSTUVWXY", 25); m += 25;
      CHECK(adv_get_name(b, (uint8_t)m, name, 8) == 7 && strcmp(name, "ABCDEFG") == 0); }
    CHECK(adv_get_name(ad, 3, name, sizeof name) == 0 && name[0] == 0);
    if (fails) { printf("%d failure(s)\n", fails); return 1; }
    printf("adv_parse tests passed\n"); return 0;
}
