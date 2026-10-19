#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""The web editor's questions from the command line, over USB-MIDI SysEx (app/editor.h):
what runs, its counters, heap and stack margins, the reset reason, and the device log.
For a unit on the bench without a browser: the first thing to read after an install.

  fm1_editor.py [--port NAME] [info] [status] [sysinfo] [log]     (default: all four)

Needs mido with python-rtmidi. Read-only: nothing here changes a setting."""
import argparse
import re
import struct
import sys
import time

import mido

PREFIX = [0x7D, ord('F'), ord('M'), ord('1')]
PORT_RE = re.compile(r"fm-1", re.I)


def unpack7(s):
    out, acc, nb = [], 0, 0
    for b in s:
        acc |= (b & 0x7F) << nb
        nb += 7
        while nb >= 8:
            out.append(acc & 0xFF)
            acc >>= 8
            nb -= 8
    return bytes(out)


class Editor:
    def __init__(self, port):
        names = [n for n in mido.get_input_names() if (port and port in n) or (not port and PORT_RE.search(n))]
        if not names:
            sys.exit(f"no MIDI port matches {port or 'fm-1'}: " + ", ".join(mido.get_input_names()))
        self.name = names[0]
        self.inp = mido.open_input(self.name)
        self.out = mido.open_output(self.name)

    def ask(self, cmd, payload=(), timeout=2.0):
        self.out.send(mido.Message("sysex", data=PREFIX + [cmd] + list(payload)))
        end = time.time() + timeout
        while time.time() < end:
            m = self.inp.poll()
            if m is None:
                time.sleep(0.005)
                continue
            if m.type == "sysex" and list(m.data[:4]) == PREFIX and m.data[4] == (cmd | 0x40):
                return bytes(m.data[5:])
        sys.exit(f"no answer to command 0x{cmd:02X} from {self.name}")

    def info(self):
        print("info:    " + self.ask(0x01).decode("latin-1"))

    def status(self):
        st = unpack7(self.ask(0x04))
        cin = struct.unpack_from("<4I", st, 0)
        cout = struct.unpack_from("<4I", st, 16)
        dropped, usb_ok, ble_ok, ble_state = struct.unpack_from("<IBBB", st, 32)
        print(f"status:  in {cin} out {cout} dropped {dropped} usb {usb_ok} ble {ble_ok} ble_state {ble_state}")

    def sysinfo(self):
        si = unpack7(self.ask(0x0B))
        up, heap, sb, su, boots, wdt, soft, hangs, reason, minimal = struct.unpack_from("<9IB", si, 0)
        print(f"sysinfo: uptime {up / 1000:.1f} s, free heap {heap}, stack free bridge {sb} ui {su} words, "
              f"boots {boots}, wdt resets {wdt}, soft resets {soft}, hangs {hangs}, last reason 0x{reason:X}, "
              f"minimal {minimal}")

    def matrix(self, force=None):
        payload = []
        if force is not None:
            word, ticks = force
            payload = [word & 0x7F, (word >> 7) & 0x7F, ticks & 0x7F, (ticks >> 7) & 0x7F]
        m = unpack7(self.ask(0x0C, payload))
        ticks, pa, pb = struct.unpack_from("<III", m, 0)
        raw = m[12:36]
        dir_, out = raw[12] | raw[13] << 8, raw[14] | raw[15] << 8
        popped = raw[16] | raw[17] << 8 | raw[18] << 16 | raw[19] << 24
        ev = {0: "down", 1: "up", 2: "hold", 3: "enc"}.get(raw[21], "?")
        print(f"matrix:  scan ticks {ticks}, port A in 0x{pa:08X}, port B in 0x{pb:08X}, closed rows per column "
              + " ".join(f"{b:02X}" for b in raw[:11]) + f" | test rows {raw[11]:02X}, port A DIR 0x{dir_:04X} OUT 0x{out:04X}"
              + f" | ui: {popped} events popped, last {ev} id {raw[20]}, backlight flag {raw[22]}")

    def matrix_all(self):
        """all eleven columns selected for 300 ms, then the rows read under it"""
        self.matrix(force=(0x0000, 3000))
        time.sleep(0.2)
        self.matrix()

    def log(self):
        from_ = 0
        text = b""
        while True:
            off = [from_ & 0x7F, (from_ >> 7) & 0x7F, (from_ >> 14) & 0x7F, (from_ >> 21) & 0x7F]
            r = unpack7(self.ask(0x0A, off))
            total, used = struct.unpack_from("<II", r, 0)
            chunk = r[8:]
            if not chunk:
                break
            if used != from_ and not text:
                print(f"log:     {total} bytes written, holding from {used}")
            text += chunk
            from_ = used + len(chunk)
            if from_ >= total:
                break
        print(text.decode("latin-1", "replace").rstrip("\n"))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("what", nargs="*", metavar="WHAT", help="info, status, sysinfo, log, matrix, matrix_all")
    ap.add_argument("--port", metavar="NAME")
    a = ap.parse_args()
    ed = Editor(a.port)
    for w in a.what or ["info", "status", "sysinfo", "log"]:
        if w not in ("info", "status", "sysinfo", "log", "matrix", "matrix_all"):
            sys.exit(f"{w}: info, status, sysinfo, log or matrix")
        getattr(ed, w)()


if __name__ == "__main__":
    main()
