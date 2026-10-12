/* SPDX-License-Identifier: MIT */
/* Byte ring for the device log. Writers (printf sink, fm1_log) append; a reader
 * asks for bytes from an absolute offset, so a host can page through what is held
 * and resume after a reconnect. Pure C, host-tested. */
#pragma once
#include <stdint.h>

#define LOGRING_SIZE 4096u

typedef struct logring {
    uint8_t buf[LOGRING_SIZE];
    uint32_t total;                 /* bytes ever written; the oldest held is total - min(total, SIZE) */
} logring;

void     logring_init(logring *r);
void     logring_put(logring *r, char c);
void     logring_write(logring *r, const char *s, uint32_t n);
uint32_t logring_total(const logring *r);
uint32_t logring_start(const logring *r);
/* copy up to max bytes starting at absolute offset from (clamped to what is held);
 * *from is updated to the offset actually used; returns the count */
uint32_t logring_read(const logring *r, uint32_t *from, uint8_t *out, uint32_t max);
