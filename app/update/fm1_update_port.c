/* SPDX-License-Identifier: MIT */
/* SDK side of the update entry: frames come from the bridge (SysEx reassembled
 * from USB-MIDI), replies go out as USB-MIDI, flash through the SDK NOR driver,
 * the record into the SDK's UPDATA_PARM RAM slot, then a CPU reset. */
#include "system/includes.h"
#include "app_config.h"
#include "os/os_api.h"
#include "asm/cpu.h"
#include "asm/sfc_norflash_api.h"
#include "device/ioctl_cmds.h"
#include "update.h"
#include "fm1_update.h"
#include "midi/midi_msg.h"
#include "midi/usbmidi.h"
#include "usb/usb_midi.h"
#include "bridge.h"
#include "fm1_log.h"

#define LOG_TAG_CONST       APP
#define LOG_TAG             "[UPDATE]"
#define LOG_ERROR_ENABLE
#define LOG_INFO_ENABLE
#include "debug.h"

static u8 frame[UPD_WIRE_MAX];
static volatile u32 frame_len;

/* the bridge hands over one complete F0..F7 message (dropped if one is pending) */
void fm1_update_port_frame(const u8 *p, u32 n)
{
    if (frame_len || n > sizeof frame) return;
    memcpy(frame, p, n);
    frame_len = n;
}

int upd_port_frame_get(const uint8_t **p, uint32_t *n)
{
    if (!frame_len) return 0;
    *p = frame;
    *n = frame_len;
    return 1;
}

void upd_port_frame_done(void) { frame_len = 0; }

int upd_port_send(const uint8_t *wire, uint32_t n)
{
    u8 out[(MIDI_SYSEX_CHUNK / 3 + 1) * 4];
    uint32_t off = 0;
    if (!usb_midi_configured(0)) return -1;
    while (off < n) {
        midi_msg m;
        uint32_t take = n - off > MIDI_SYSEX_CHUNK ? MIDI_SYSEX_CHUNK : n - off;
        u16 w;
        m.kind = MIDI_KIND_SYSEX;
        m.flags = (u8)((off == 0 ? MIDI_SYSEX_START : 0) | (off + take == n ? MIDI_SYSEX_END : 0));
        m.len = (u8)take;
        memcpy(m.b, wire + off, take);
        w = usbmidi_encode(0, &m, out);
        if (usb_midi_write(0, out, w) != w) return -1;
        off += take;
    }
    return 0;
}

uint32_t upd_port_now_ms(void) { return timer_get_ms(); }

void upd_port_idle(void)
{
    extern void health_bridge_alive(void);
    health_bridge_alive();
    if (!bridge_pump_usb()) os_time_dly(1);
}

extern int norflash_erase(u32 cmd, u32 addr);   /* in cpu.a, used by apps/common/fat_nor */
int upd_port_flash_erase(uint32_t off) { return norflash_erase(IOCTL_ERASE_SECTOR, off); }
int upd_port_flash_prog(uint32_t off, const void *p, uint32_t n) { return norflash_write(NULL, (void *)p, n, off) == (int)n ? 0 : -1; }
int upd_port_flash_read(uint32_t off, void *p, uint32_t n) { return norflash_origin_read(p, off, n) == (int)n ? 0 : -1; }

void upd_port_show(uint32_t step, int32_t code)
{
    log_info("update step %u code %d", step, code);
    fm1_log("update step %u code %d", (unsigned)step, (int)code);
}

void upd_port_commit(const uint8_t *parm)
{
    log_info("update: record written, resetting into the loader");
    fm1_log("update: record written, resetting into the loader");
    os_time_dly(3);
    local_irq_disable();
    memcpy(UPDATA_FLAG_ADDR, parm, UPD_PARM_LEN);
    cpu_reset();
    while (1);
}

void upd_port_uboot(void)
{
    extern void go_mask_usb_updata(void);
    log_info("soft key: mask-ROM USB update mode");
    fm1_log("soft key: resetting into mask-ROM USB update mode");
    os_time_dly(3);
    go_mask_usb_updata();
}
