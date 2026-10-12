/* SPDX-License-Identifier: MIT */
#include "logring.h"
#include <string.h>

void logring_init(logring *r) { memset(r, 0, sizeof *r); }

void logring_put(logring *r, char c)
{
    r->buf[r->total % LOGRING_SIZE] = (uint8_t)c;
    r->total++;
}

void logring_write(logring *r, const char *s, uint32_t n)
{
    while (n--) logring_put(r, *s++);
}

uint32_t logring_total(const logring *r) { return r->total; }

uint32_t logring_start(const logring *r)
{
    return r->total > LOGRING_SIZE ? r->total - LOGRING_SIZE : 0;
}

uint32_t logring_read(const logring *r, uint32_t *from, uint8_t *out, uint32_t max)
{
    uint32_t start = logring_start(r), end = r->total, off, n, i;
    off = *from;
    if (off < start) off = start;
    if (off > end) off = end;
    *from = off;
    n = end - off;
    if (n > max) n = max;
    for (i = 0; i < n; i++) out[i] = r->buf[(off + i) % LOGRING_SIZE];
    return n;
}
