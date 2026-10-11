/* SPDX-License-Identifier: MIT */
#include "crc16.h"

uint32_t crc16_1021(const uint8_t *p, uint32_t n, uint32_t c)
{
    uint32_t i;
    while (n--) {
        c ^= (uint32_t)*p++ << 8;
        for (i = 0; i < 8; i++)
            c = (c & 0x8000u) ? ((c << 1) ^ 0x1021u) : (c << 1);
        c &= 0xFFFFu;
    }
    return c;
}
