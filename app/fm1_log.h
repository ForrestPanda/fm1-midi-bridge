/* SPDX-License-Identifier: MIT */
/* Device log: a RAM ring the web editor reads over SysEx (editor command 0x0A).
 * There are no debug pads on the FM-1 and PC3 (the SDK's debug UART TX) is not
 * known to be free, so this is the console. Two sources feed it:
 *   - fm1_log(): our own lines, timestamped, written straight into the ring;
 *   - log_putbyte(): the SDK log framework's per-byte user hook (printf, log_info
 *     from SDK code), which this module defines. Whether the vendor library calls
 *     it for every byte is to be verified on hardware; our own lines do not depend on it. */
#pragma once
#include <stdint.h>
#include "util/logring.h"

void fm1_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
const logring *fm1_log_ring(void);

/* task stack margins, recorded by the tasks themselves (uxTaskGetStackHighWaterMark) */
enum { FM1_TASK_BRIDGE = 0, FM1_TASK_UI = 1, FM1_TASK_COUNT };
void     fm1_log_stack_note(int task);          /* call from inside that task now and then */
uint32_t fm1_log_stack_free(int task);          /* words, 0 = never recorded */
