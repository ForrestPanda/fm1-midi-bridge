/* SPDX-License-Identifier: MIT */
/* Host stand-in for the SDK's circular buffer (lib/system, closed source). Same
 * contract as the library, read from its bitcode: cbuf_write writes all of len or
 * nothing (returns 0), cbuf_read reads all of len or nothing. */
#pragma once
#include "typedef.h"
typedef struct _cbuffer { u8 *buf; u32 size, rd, wr, len; } cbuffer_t;
void cbuf_init(cbuffer_t *c, void *buf, u32 size);
u32  cbuf_write(cbuffer_t *c, void *buf, u32 len);
u32  cbuf_read(cbuffer_t *c, void *buf, u32 len);
u32  cbuf_get_data_len(cbuffer_t *c);
void cbuf_clear(cbuffer_t *c);
