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
void log_putbyte(char c)
{
    if (c == '\r') return;
    logring_put(&ring, c);
}

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
