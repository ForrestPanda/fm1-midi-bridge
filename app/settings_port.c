/* SPDX-License-Identifier: MIT */
#include "system/includes.h"
#include "syscfg_id.h"
#include "settings.h"

int settings_port_read(uint16_t id, void *buf, uint16_t len)        { return syscfg_read(id, buf, len); }
int settings_port_write(uint16_t id, const void *buf, uint16_t len) { return syscfg_write(id, (void *)buf, len); }
uint32_t settings_port_now_ms(void)                    { return timer_get_ms(); }
