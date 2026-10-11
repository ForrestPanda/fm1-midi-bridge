/* SPDX-License-Identifier: MIT */
/* CRC-16, polynomial 0x1021, init 0, no reflection: the one JieLi uses everywhere. */
#pragma once
#include <stdint.h>
uint32_t crc16_1021(const uint8_t *p, uint32_t n, uint32_t c);
