/* SPDX-License-Identifier: MIT */
/* FM-1 update entry (step 1 of the M-VAVE USB-MIDI SysEx update), portable core.
 *
 * Protocol (documented in docs/update-protocol.md; the same one the stock
 * M-UPGRADE app, Felucca's installer and Felucca's loader speak):
 *   frame      F0 pack7(00 59 cmd len24 payload chk) F7     chk = ~sum(payload)
 *   0x11       handshake: we answer our identity (27 bytes, "FM-1_9B1" zero padded)
 *   raw F0 22 24 35 7F F7   upgrade: run a session (below)
 *   raw F0 22 24 35 7D F7   soft key: reboot into the mask-ROM USB download mode
 *   0x30       our read request (flags, addr32, len24); the host answers with the
 *              bytes of the package's logical image at that offset
 *   addr 0xE0000000, 8 bytes "success\0"   the host confirms step 1
 * Session: read the UFW header, find ota.bin (the update loader), check its heads,
 * erase the staging area, write the loader body, verify, write the loader head,
 * ask the host for success, write the UPDATA_PARM record (flash copy + RAM) and
 * reset. The SPL then runs the loader, which does step 2 with the host.
 * Nothing is committed before the host's success; any failure erases the staging
 * area and the app carries on.
 *
 * The platform supplies the upd_port_* hooks (fm1_update_port.c on the SDK,
 * tests/test_update.c on the host).
 */
#pragma once
#include <stdint.h>

#ifndef UPD_IDENTITY
#define UPD_IDENTITY        "FM-1_9B1"          /* must equal the package identity (tools/package.sh); the minimal build says FM-1_9B0 */
#endif
#define UPD_OTA_AREA        0x000E0000u         /* staged loader: 5 x 4 KiB sectors */
#define UPD_OTA_AREA_LEN    0x5000u
#define UPD_OTA_RES         (UPD_OTA_AREA + UPD_OTA_AREA_LEN - 0x100u)   /* record copy the SPL scans */
#define UPD_FRAME_MAX       640u                /* decoded host frame (512-byte read + 15) */
#define UPD_WIRE_MAX        (2u + (UPD_FRAME_MAX * 8u + 6u) / 7u)
#define UPD_PARM_LEN        112u

/* --- platform hooks --- */
int  upd_port_send(const uint8_t *wire, uint32_t n);          /* one complete F0..F7 message to the host */
int  upd_port_frame_get(const uint8_t **p, uint32_t *n);      /* next received SysEx incl. F0/F7, 0 if none */
void upd_port_frame_done(void);
uint32_t upd_port_now_ms(void);
void upd_port_idle(void);                                     /* called while waiting: pump USB, feed watchdog */
int  upd_port_flash_erase(uint32_t off);                      /* one 4 KiB sector, 0 = ok */
int  upd_port_flash_prog(uint32_t off, const void *p, uint32_t n);
int  upd_port_flash_read(uint32_t off, void *p, uint32_t n);  /* raw bytes */
void upd_port_show(uint32_t step, int32_t code);              /* progress / result, for a screen or log */
void upd_port_commit(const uint8_t *parm);                    /* RAM record + reset; never returns on hardware */
void upd_port_uboot(void);                                    /* soft key: mask-ROM USB mode; never returns */

/* --- core --- */
void fm1_update_boot_cleanup(void);     /* at boot: erase a leftover staging area */
int  fm1_update_poll(void);             /* handle one queued frame; may run a whole session; 1 = handled */
int  fm1_update_busy(void);

/* --- helpers shared with the tests --- */
uint32_t upd_crc16(const uint8_t *p, uint32_t n, uint32_t c);
uint32_t upd_pack7(const uint8_t *in, uint32_t n, uint8_t *out);
uint32_t upd_unpack7(const uint8_t *in, uint32_t n, uint8_t *out, uint32_t max);
