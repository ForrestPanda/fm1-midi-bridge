/* SPDX-License-Identifier: MIT */
#include "adv_parse.h"
#include <string.h>

const uint8_t midi_service_uuid_le[16] = {
    0x00, 0xC7, 0xC4, 0x4E, 0xE3, 0x6C, 0x51, 0xA7, 0x33, 0x4B, 0xE8, 0xED, 0x5A, 0x0E, 0xB8, 0x03};

/* walk AD structures: [len][type][data...]; cb returns 1 to stop */
static int walk(const uint8_t *ad, uint8_t len, int (*cb)(uint8_t type, const uint8_t *d, uint8_t n, void *ctx), void *ctx)
{
    uint8_t i = 0;
    while (i < len) {
        uint8_t l = ad[i];
        if (l == 0 || (unsigned)i + 1u + l > (unsigned)len) break;   /* malformed: stop */
        if (cb(ad[i + 1], ad + i + 2, (uint8_t)(l - 1), ctx)) return 1;
        i = (uint8_t)(i + 1 + l);
    }
    return 0;
}

static int cb_service(uint8_t type, const uint8_t *d, uint8_t n, void *ctx)
{
    (void)ctx;
    if (type == 0x06 || type == 0x07) {
        uint8_t k;
        for (k = 0; k + 16 <= n; k = (uint8_t)(k + 16))
            if (memcmp(d + k, midi_service_uuid_le, 16) == 0) return 1;
    }
    return 0;
}

int adv_has_midi_service(const uint8_t *ad, uint8_t len)
{
    return walk(ad, len, cb_service, NULL);
}

struct namectx { char *name; int n; int got; int complete; };

static int cb_name(uint8_t type, const uint8_t *d, uint8_t n, void *ctx)
{
    struct namectx *c = ctx;
    if (type == 0x09 || (type == 0x08 && !c->complete)) {
        int k = n < c->n - 1 ? n : c->n - 1;
        memcpy(c->name, d, (size_t)k);
        c->name[k] = 0;
        c->got = k;
        c->complete = type == 0x09;
        return c->complete;
    }
    return 0;
}

int adv_get_name(const uint8_t *ad, uint8_t len, char *name, int n)
{
    struct namectx c = { name, n, 0, 0 };
    if (n <= 0) return 0;
    name[0] = 0;
    walk(ad, len, cb_name, &c);
    return c.got;
}
