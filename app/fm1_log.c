/* SPDX-License-Identifier: MIT */
#include "system/includes.h"
#include "app_config.h"
#include "fm1_log.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static logring ring;
static uint32_t stack_free[FM1_TASK_COUNT];

const logring *fm1_log_ring(void) { return &ring; }

/* SDK log framework user hook (apps/common/debug/debug.c leaves it to us under
 * CONFIG_USER_DEBUG_ENABLE). May run from any task or interrupt: a single byte
 * store and a counter bump, a torn byte under contention is acceptable here. */
/* The SDK's library prints a task and heap table every minute (about 20 lines, seen on the
 * first unit): it would push everything else out of the 4 KB ring within three minutes, so
 * its lines are held back here. The block starts at a line holding "task run" and ends with
 * the "[HEAP_MEM]left" line. Lines are gathered first; one longer than the buffer goes out in
 * pieces, and two tasks printing at once may tear a line, which is acceptable here. */
static char cur[160];
static uint16_t cur_n;
static uint8_t dropping;

static int line_has(const char *needle)
{
    uint32_t k = (uint32_t)strlen(needle), i;
    for (i = 0; i + k <= cur_n; i++)
        if (memcmp(cur + i, needle, k) == 0) return 1;
    return 0;
}

void log_putbyte(char c)
{
    if (c == '\r') return;
    if (cur_n < sizeof cur) cur[cur_n++] = c;
    if (c != '\n' && cur_n < sizeof cur) return;
    if (dropping) { if (line_has("[HEAP_MEM]left")) dropping = 0; }
    else if (line_has("task run")) dropping = 1;
    else logring_write(&ring, cur, cur_n);
    cur_n = 0;
}

void fm1_log_raw(const char *s, uint32_t n) { logring_write(&ring, s, n); }

void fm1_log(const char *fmt, ...)
{
    char line[128];
    int n, k;
    va_list ap;
    n = snprintf(line, sizeof line, "[%6u] ", (unsigned)timer_get_ms());
    va_start(ap, fmt);
    k = vsnprintf(line + n, sizeof line - (size_t)n, fmt, ap);
    va_end(ap);
    if (k < 0) k = 0;
    n += k;
    if (n > (int)sizeof line - 2) n = (int)sizeof line - 2;
    line[n++] = '\n';
    logring_write(&ring, line, (uint32_t)n);
}

extern unsigned long uxTaskGetStackHighWaterMark(void *task);   /* FreeRTOS, INCLUDE_uxTaskGetStackHighWaterMark 1 */

void fm1_log_stack_note(int task)
{
    if (task >= 0 && task < FM1_TASK_COUNT) stack_free[task] = (uint32_t)uxTaskGetStackHighWaterMark(NULL);
}

uint32_t fm1_log_stack_free(int task)
{
    return task >= 0 && task < FM1_TASK_COUNT ? stack_free[task] : 0;
}
