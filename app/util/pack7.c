/* SPDX-License-Identifier: MIT */
#include "pack7.h"

uint32_t pack7(const uint8_t *in, uint32_t n, uint8_t *out)
{
    uint32_t acc = 0, nb = 0, o = 0;
    while (n--) {
        acc |= (uint32_t)*in++ << nb;
        nb += 8;
        while (nb >= 7) { out[o++] = (uint8_t)(acc & 0x7Fu); acc >>= 7; nb -= 7; }
    }
    if (nb) out[o++] = (uint8_t)(acc & 0x7Fu);
    return o;
}

uint32_t unpack7(const uint8_t *in, uint32_t n, uint8_t *out, uint32_t max)
{
    uint32_t acc = 0, nb = 0, o = 0;
    while (n--) {
        acc |= (uint32_t)(*in++ & 0x7Fu) << nb;
        nb += 7;
        if (nb >= 8) { if (o < max) out[o] = (uint8_t)acc; o++; acc >>= 8; nb -= 8; }
    }
    return o;
}
