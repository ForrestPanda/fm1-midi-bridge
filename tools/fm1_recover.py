#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Put an FM-1 that still enumerates on USB into the chip's mask-ROM USB download
mode (UBOOT1.00), where jl-uboot-tool can rewrite the flash (tools/fm1_flash.sh).

Three doors, tried in order unless one is chosen:
  --softkey   the M-VAVE soft key over MIDI SysEx  F0 22 24 35 7D F7   (stock, Felucca, this bridge)
  --vendor    the SDK's vendor control request on endpoint 0: GET_STATUS wValue 0xFC wIndex 0xFE
              (any firmware built on the AC79 SDK USB device stack, this bridge included; needs pyusb
              and permission on the USB device: run as root or add a udev rule for 3654:*)
  --wait      only wait for the UBOOT disk to appear and print its /dev/sg path
Exit codes: 0 UBOOT device seen, 1 nothing worked, 2 missing module.
--softkey is verified on stock V15 (2026-10-19) and on this bridge (2026-10-20): the ROM shows
within seconds; back out: switch off, unplug, plug in, switch on. --vendor is not verified on
hardware yet; it follows the SDK's user_setup.c. A firmware that never enumerates needs the
D+/D- dongle.
"""
import argparse
import glob
import os
import re
import sys
import time

SOFTKEY = [0xF0, 0x22, 0x24, 0x35, 0x7D, 0xF7]
PORT_RE = re.compile(r"fm-1|fm1|felucca|composite|usb-midi", re.I)
JIELI_VID = 0x3654


def scsi_devices():
    """(model, vendor, sg path) for every SCSI device the kernel knows"""
    out = []
    for dev in glob.glob("/sys/class/scsi_device/*/device"):
        try:
            model = open(os.path.join(dev, "model")).read().strip()
            vendor = open(os.path.join(dev, "vendor")).read().strip()
        except OSError:
            continue
        sg = glob.glob(os.path.join(dev, "scsi_generic", "sg*"))
        out.append((model, vendor, "/dev/" + os.path.basename(sg[0]) if sg else "?"))
    return out


def uboot_device():
    for model, vendor, sg in scsi_devices():
        if model.upper().startswith(("UBOOT", "UDISK", "DEVICE")):
            return model, vendor, sg
    return None


def wait_uboot(seconds):
    t0 = time.time()
    while time.time() - t0 < seconds:
        d = uboot_device()
        if d:
            return d
        time.sleep(0.5)
    return None


def softkey(port):
    try:
        import mido
    except ImportError:
        print("softkey: pip3 install mido python-rtmidi", file=sys.stderr)
        return False
    names = [n for n in mido.get_output_names() if (port and port.lower() in n.lower()) or (not port and PORT_RE.search(n))]
    if not names:
        print("softkey: no FM-1 MIDI port (have: %s)" % ", ".join(mido.get_output_names() or ["none"]))
        return False
    with mido.open_output(names[0]) as o:
        o.send(mido.Message("sysex", data=SOFTKEY[1:-1]))
    print("softkey: sent to %s" % names[0])
    return True


def vendor():
    try:
        import usb.core
    except ImportError:
        print("vendor: pip3 install pyusb", file=sys.stderr)
        return False
    found = []
    for d in usb.core.find(find_all=True):
        try:
            prod = d.product or ""
            manu = d.manufacturer or ""
        except (usb.core.USBError, ValueError):
            prod = manu = ""
        if d.idVendor == JIELI_VID or re.search(r"fm-1|m-vave|jieli", prod + " " + manu, re.I):
            found.append((d, prod, manu))
    if not found:
        print("vendor: no JieLi / FM-1 USB device")
        return False
    d, prod, manu = found[0]
    print("vendor: %04x:%04x %s %s" % (d.idVendor, d.idProduct, manu, prod))
    try:
        ack = d.ctrl_transfer(0x80, 0x00, 0xFC, 0xFE, 8, timeout=1000)   # GET_STATUS, private wValue/wIndex, 8 bytes
    except usb.core.USBError as e:
        print("vendor: probe refused (%s): not an SDK USB stack, or no permission" % e)
        return False
    if list(ack) != [0xFF] * 8:
        print("vendor: unexpected probe answer %s" % list(ack))
        return False
    print("vendor: device supports the private update command, sending it")
    try:
        d.ctrl_transfer(0x80, 0x00, 0xFC, 0xFE, 0, timeout=1000)         # wLength 0: go_mask_usb_updata()
    except usb.core.USBError:
        pass                                                           # it resets mid-request
    return True


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--softkey", action="store_true")
    ap.add_argument("--vendor", action="store_true")
    ap.add_argument("--wait", action="store_true")
    ap.add_argument("--port", help="MIDI port name fragment for --softkey")
    ap.add_argument("--timeout", type=float, default=20, help="seconds to wait for UBOOT after each door")
    a = ap.parse_args()
    d = uboot_device()
    if d:
        print("already in UBOOT mode: %s %s at %s" % (d[1], d[0], d[2]))
        return 0
    doors = []
    if a.softkey:
        doors.append(softkey)
    if a.vendor:
        doors.append(vendor)
    if not doors and not a.wait:
        doors = [softkey, vendor]
    for door in doors:
        if door(a.port) if door is softkey else door():
            d = wait_uboot(a.timeout)
            if d:
                print("UBOOT: %s %s at %s  (now: tools/fm1_flash.sh dump FILE)" % (d[1], d[0], d[2]))
                return 0
            print("no UBOOT disk within %.0f s" % a.timeout)
    if a.wait:
        d = wait_uboot(a.timeout)
        if d:
            print("UBOOT: %s %s at %s" % (d[1], d[0], d[2]))
            return 0
    print("not in UBOOT mode. Next: the D+/D- dongle at power-up (docs/recovery.md).")
    return 1


if __name__ == "__main__":
    sys.exit(main())
