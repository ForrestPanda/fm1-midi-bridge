/* SPDX-License-Identifier: MIT */
#pragma once
#include "typedef.h"
int  os_task_create(void (*func)(void *parm), void *parm, int prio, int stack_words, int qsize, const char *name);
void os_time_dly(int ticks);            /* 10 ms per tick */
typedef struct { int cnt; } OS_SEM;
int  os_sem_create(OS_SEM *sem, int cnt);
int  os_sem_pend(OS_SEM *sem, int timeout_ticks);   /* 0 = taken, -1 = timed out */
int  os_sem_post(OS_SEM *sem);
