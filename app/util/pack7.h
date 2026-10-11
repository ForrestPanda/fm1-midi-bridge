/* SPDX-License-Identifier: MIT */
/* 8-bit <-> 7-bit packing for SysEx payloads, LSB first (the M-VAVE / JieLi way). */
#pragma once
#include <stdint.h>
uint32_t pack7(const uint8_t *in, uint32_t n, uint8_t *out);                 /* returns packed length */
uint32_t unpack7(const uint8_t *in, uint32_t n, uint8_t *out, uint32_t max);  /* returns unpacked length */
