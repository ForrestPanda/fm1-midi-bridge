/* SPDX-License-Identifier: MIT */
/* 8-to-7 packing used by the update protocol and the editor: unpack anything,
 * and pack/unpack must round-trip. */
#include "../../app/util/pack7.h"
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t n)
{
    uint8_t out[4096], packed[4096 + 4096 / 7 + 2], back[4096];
    uint32_t k, pl, ul;
    if (n > 2048) n = 2048;
    k = unpack7(data, (uint32_t)n, out, (uint32_t)(n < 64 ? n : 64));      /* small max: clips the writes, still counts */
    ul = unpack7(data, (uint32_t)n, out, sizeof out);
    if (k != ul || ul > n) abort();
    pl = pack7(data, (uint32_t)n, packed);
    if (pl > sizeof packed) abort();
    ul = unpack7(packed, pl, back, sizeof back);
    if (ul != n || memcmp(back, data, n) != 0) abort();
    return 0;
}

const uint8_t *fuzz_seed(int i, size_t *n)
{
    static const uint8_t s0[] = {0, 0x59, 0x11, 0, 0, 0, 0xFF, 0x80, 0x7F, 1, 2, 3, 4, 5, 6, 7};
    if (i == 0) { *n = sizeof s0; return s0; }
    return NULL;
}
