/* SPDX-License-Identifier: Apache-2.0 */
/* USB-MIDI 1.0 device class for the JieLi AC79 SDK USB device stack.
 * Written after the SDK's apps/common/usb/device/cdc.c and hid.c (Apache-2.0).
 *
 * One AudioControl interface (no endpoints) + one MIDIStreaming interface with
 * one bulk OUT (host -> device) and one bulk IN (device -> host) endpoint, one
 * embedded MIDI IN jack and one embedded MIDI OUT jack: a single virtual cable.
 * Data on the wire are 4-byte USB-MIDI event packets (app/midi/usbmidi.h).
 */
#pragma once
#include "typedef.h"
#include "usb/device/usb_stack.h"

#define MIDI_CLASS              0x00000100   /* our class bit for USB_DEVICE_CLASS_CONFIG */
#define MIDI_EP_IN              1
#define MIDI_EP_OUT             1
#define MAXP_SIZE_MIDI_BULK     64

/* desc_config callback for usb_add_desc_config() */
u32 usb_midi_desc_config(const usb_dev usb_id, u8 *ptr, u32 *cur_itf_num);
void usb_midi_register(const usb_dev usb_id);
void usb_midi_release(const usb_dev usb_id);
/* host -> device data arrives in USB interrupt context through this handler:
 * keep it short (copy into a ring, signal a task). len is a multiple of 4. */
void usb_midi_set_rx_handler(const usb_dev usb_id, void *priv, void (*rx)(void *priv, const u8 *pkts, u32 len));
/* device -> host: len a multiple of 4; returns bytes queued (0 if not configured) */
u32 usb_midi_write(const usb_dev usb_id, const u8 *pkts, u32 len);
int usb_midi_configured(const usb_dev usb_id);
