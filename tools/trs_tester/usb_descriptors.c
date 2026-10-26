/* SPDX-License-Identifier: MIT */
/* USB descriptors: one USB-MIDI 1.0 interface, "FM-1 TRS Tester" (bcdDevice 2.00). Adapted,
 * through the author's TRS MIDI bridge project, from TinyUSB's examples/device/midi_test/src/usb_descriptors.c,
 * whose notice follows (full text: LICENSES/MIT-TinyUSB.txt).
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2019 Ha Thach (tinyusb.org)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE. */

#include <string.h>

#include "board.h"
#include "tusb.h"

// pid.codes test VID/PID (https://pid.codes/1209/0001/). Fine for personal
// use; get your own PID before distributing devices.
#define USB_VID 0x1209
#define USB_PID 0x0001

static const tusb_desc_device_t desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,
    .bDeviceClass       = 0x00,
    .bDeviceSubClass    = 0x00,
    .bDeviceProtocol    = 0x00,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = USB_VID,
    .idProduct          = USB_PID,
    .bcdDevice          = 0x0200,
    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,
    .bNumConfigurations = 0x01,
};

const uint8_t *tud_descriptor_device_cb(void) { return (const uint8_t *)&desc_device; }

enum { ITF_NUM_MIDI = 0, ITF_NUM_MIDI_STREAMING, ITF_NUM_TOTAL };

#define EPNUM_MIDI_OUT   0x01
#define EPNUM_MIDI_IN    0x81
#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_MIDI_DESC_LEN)

static const uint8_t desc_configuration[] = {
    // config number, interface count, string index, total length, attributes, power in mA
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),
    // interface number, string index, EP OUT & EP IN address, EP size
    TUD_MIDI_DESCRIPTOR(ITF_NUM_MIDI, 0, EPNUM_MIDI_OUT, EPNUM_MIDI_IN, 64),
};

const uint8_t *tud_descriptor_configuration_cb(uint8_t index) {
  (void)index;
  return desc_configuration;
}

enum { STRID_LANGID = 0, STRID_MANUFACTURER, STRID_PRODUCT, STRID_SERIAL };

static const char *const string_desc_arr[] = {
    [STRID_MANUFACTURER] = "DIY",
    [STRID_PRODUCT]      = "FM-1 TRS Tester",
};

static uint16_t desc_str[32 + 1];

const uint16_t *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
  (void)langid;
  uint32_t chr_count;

  switch (index) {
    case STRID_LANGID:
      desc_str[1] = 0x0409; // English (US)
      chr_count   = 1;
      break;

    case STRID_SERIAL:
      chr_count = board_serial_utf16(desc_str + 1, 32);
      break;

    case STRID_MANUFACTURER:
    case STRID_PRODUCT: {
      const char *str = string_desc_arr[index];
      chr_count       = (uint32_t)strlen(str);
      if (chr_count > 32) chr_count = 32;
      for (uint32_t i = 0; i < chr_count; i++) desc_str[1 + i] = (uint16_t)str[i];
      break;
    }

    default:
      return NULL;
  }

  desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));
  return desc_str;
}
