/* SPDX-License-Identifier: MIT */
#pragma once
#include "typedef.h"
void *dev_open(const char *name, void *arg);
int   dev_read(void *dev, void *buf, u32 len);
int   dev_ioctl(void *dev, u32 cmd, u32 arg);
