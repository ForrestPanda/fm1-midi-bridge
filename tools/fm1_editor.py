#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""The web editor's questions from the command line, over USB-MIDI SysEx (app/editor.h):
what runs, its counters, heap and stack margins, the reset reason, and the device log.
For a unit on the bench without a browser: the first thing to read after an install.

  fm1_editor.py [--port NAME] [info] [status] [sysinfo] [log] [settings]     (default: info status sysinfo log)
  fm1_editor.py set FIELD=VALUE ...        write byte fields of the settings (ble_mode=1: host mode)
  fm1_editor.py send PORT HEX ...          raw MIDI bytes to port 1 = USB, 2 = BLE, 3 = both (send 2 90 3C 64)
  fm1_editor.py host [connect N | disconnect | scan on|off]     host mode: the scan list, and the Scan page's buttons
  fm1_editor.py names [SLOT "NAME"]        the presets' names; with SLOT (1..4) and a name, set it ("" clears)

Needs mido with python-rtmidi. Read-only except `set` and `send`; `set` reads the settings back
from the device, changes the named bytes and writes the blob, which the device applies at once
and stores a second later (the editor keeps the device's own host_peer and CRC)."""
import argparse
import re
import struct
import sys
import time

import mido

PREFIX = [0x7D, ord('F'), ord('M'), ord('1')]
PORT_RE = re.compile(r"fm-1", re.I)


# byte fields of fm1_settings (app/settings.h v9; the same offsets as web/editor.html, checked by tests/test_editor.c)
FIELDS = {"filters": 8, "usb_enabled": 9, "backlight": 10, "invert": 11, "ble_mode": 12, "ble_host_pair": 13,
          "ble_link": 21, "kbd_enabled": 22, "kbd_channel": 23, "kbd_octave": 24, "kbd_velocity": 25,
          "panic_on_disconnect": 180, "clock_to": 204, "merge_guard": 205, "kbd_vel_source": 206, "ble_host_auto": 207,
          "log_hide": 208}
OFF_HOST_PEER, OFF_ROUTES, OFF_CLOCK_BPM = 14, 6, 202


def pack7(b):
    out, acc, nb = [], 0, 0
    for x in b:
        acc |= x << nb
        nb += 8
        while nb >= 7:
            out.append(acc & 0x7F)
            acc >>= 7
            nb -= 7
    if nb:
        out.append(acc & 0x7F)
    return out


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

    def settings(self):
        st = unpack7(self.ask(0x02))
        peer = st[OFF_HOST_PEER:OFF_HOST_PEER + 7]
        fields = " ".join(f"{k}={st[o]}" for k, o in FIELDS.items())
        print(f"settings: version {st[4]} size {st[5]} routes 0x{st[OFF_ROUTES] | st[OFF_ROUTES + 1] << 8:04X} "
              f"clock_bpm {st[OFF_CLOCK_BPM] | st[OFF_CLOCK_BPM + 1] << 8} {fields} host_peer "
              + ("none" if not any(peer[1:]) else f"type {peer[0]} " + ":".join(f"{x:02X}" for x in peer[:0:-1])))

    def set(self, assignments):
        st = bytearray(unpack7(self.ask(0x02)))
        for a in assignments:
            name, _, value = a.partition("=")
            if name not in FIELDS or not value:
                sys.exit(f"set: {a}: FIELD=VALUE with FIELD one of " + ", ".join(FIELDS))
            st[FIELDS[name]] = int(value, 0) & 0xFF
        status = self.ask(0x03, pack7(st), timeout=6.0)[0]     # a BLE mode switch inside the apply takes up to 2.5 s
        print("set:     " + {0: "ok, applied", 1: "rejected: short", 2: "rejected: bad magic or size", 3: "rejected: wrong version"}.get(status, f"status {status}"))
        if status:
            sys.exit(1)

    def send(self, port, hexbytes):
        data = bytes(int(h, 16) for h in hexbytes)
        status = self.ask(0x09, [int(port) & 0x7F] + pack7(data))[0]
        print(f"send:    {len(data)} bytes to port {port}: " + ("ok" if status == 0 else f"status {status}"))

    HOST_STATES = ["off", "starting", "idle", "scanning", "connecting", "discovering", "connected", "disconnecting"]

    def host(self, op=0, arg=0):
        r = unpack7(self.ask(0x0D, [op & 0x7F, arg & 0x7F] if op else []))
        state, count = r[0], r[1]
        name = self.HOST_STATES[state] if state < len(self.HOST_STATES) else str(state)
        print(f"host:    {name}, {count} device(s) found" + ("" if op == 0 else f" (after op {op} {arg})"))
        for i in range(count):
            d = r[2 + i * 28:2 + (i + 1) * 28]
            addr = ":".join(f"{x:02X}" for x in d[6:0:-1])
            rssi = struct.unpack("b", d[7:8])[0]
            name = d[8:28].split(b"\0", 1)[0].decode("latin-1")
            print(f"         {i}: {addr} type {d[0]} rssi {rssi:4d}  {name}")

    def names(self, slot=None, name=""):
        payload = () if slot is None else [slot] + [ord(c) for c in name if 0x20 <= ord(c) <= 0x7E][:12]
        r = self.ask(0x0E, payload)
        names = [r[1 + i * 12:13 + i * 12].split(b"\0", 1)[0].decode("ascii", "replace") for i in range(4)]
        print("names:   " + ("" if r[0] == 0 else f"(refused: {r[0]}) ") + "  ".join(f"{i + 1}: {n or '-'}" for i, n in enumerate(names)))

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
    ap.add_argument("what", nargs="*", metavar="WHAT", help="info, status, sysinfo, log, settings, matrix, matrix_all, set FIELD=VALUE ..., send PORT HEX ...")
    ap.add_argument("--port", metavar="NAME")
    a = ap.parse_args()
    ed = Editor(a.port)
    words = a.what or ["info", "status", "sysinfo", "log"]
    while words:
        w = words.pop(0)
        if w == "set":
            n = 0
            while n < len(words) and "=" in words[n]:
                n += 1
            ed.set(words[:n])
            del words[:n]
        elif w == "send":
            ed.send(words[0], words[1:])
            words = []
        elif w == "host":
            sub = words.pop(0) if words and words[0] in ("connect", "disconnect", "scan") else ""
            if sub == "connect":
                ed.host(1, int(words.pop(0)))
            elif sub == "disconnect":
                ed.host(2)
            elif sub == "scan":
                ed.host(3, 1 if words.pop(0) in ("on", "1") else 0)
            else:
                ed.host()
        elif w == "names":
            if words and words[0].isdigit():
                slot = int(words.pop(0)) - 1
                ed.names(slot, words.pop(0) if words else "")
            else:
                ed.names()
        elif w in ("info", "status", "sysinfo", "log", "settings", "matrix", "matrix_all"):
            getattr(ed, w)()
        else:
            sys.exit(f"{w}: info, status, sysinfo, log, settings, matrix, matrix_all, set or send")


if __name__ == "__main__":
    main()
