#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""The FM-1's TRS MIDI IN under test. An STM32F4-Discovery running tools/trs_tester/ ("FM-1 TRS
Tester" on USB) plays a byte script on the TRS line with microsecond timing; this tool writes the
scripts, works out what the FM-1 should forward (a port of its parser, app/midi/midi_msg.c, and its
merge guard, app/midi/merge.c), and compares that with what comes out of the FM-1's USB port, read
raw from ALSA (/dev/snd/midiCxD0, so undefined statuses are seen too) and, with --ble, out of its
BLE MIDI characteristic with the PC as the central.

  tools/trs_test.py [TEST ...] [--ble] [--addr ADDR] [--list]

Tests (all by default): notes, clock120, clock300, saturate, running, realtime, sysex, garbage,
baud, break. Each prints the messages expected and received, what is missing or extra, the order,
and the latency from the last byte on the TRS line to the PC (both sides' USB adds up to ~1 ms).
Exits 1 when a test fails.

Wiring, the FM-1's MIDI IN is Type A: tip (sink) to PA2, ring (source) to PA3, which the tester
drives high; sleeve to GND or open. The FM-1 with its default routes (TRS to USB and BLE, clock and SysEx passed), no program
holding its USB MIDI port (the raw device is opened exclusively), and for --ble in peripheral mode
with nothing connected over BLE. Needs mido with python-rtmidi; for --ble python3-dbus and gi."""
import argparse
import os
import struct
import sys
import threading
import time

import mido

HERE = os.path.dirname(os.path.abspath(__file__))
PREFIX = [0x7D, 0x46, 0x54]
BYTE_US = 320.0                                      # 10 bits at 31250 baud


# ------------------------------------------------------------------ the tester

def pack7(b):
    out = []
    for i in range(0, len(b), 7):
        g = b[i:i + 7]
        out.append(sum(((x >> 7) & 1) << k for k, x in enumerate(g)))
        out.extend(x & 0x7F for x in g)
    return out


def unpack7(s):
    out, i = [], 0
    while i < len(s):
        hi = s[i]
        i += 1
        for k in range(7):
            if i >= len(s):
                break
            out.append(s[i] | (((hi >> k) & 1) << 7))
            i += 1
    return bytes(out)


class Tester:
    def __init__(self):
        name = next((n for n in mido.get_output_names() if "TRS Tester" in n), None)
        if not name:
            sys.exit("no FM-1 TRS Tester on USB (tools/trs_tester, the Discovery's CN5 to the PC)")
        self.out = mido.open_output(name)
        self.inp = mido.open_input(next(n for n in mido.get_input_names() if "TRS Tester" in n))
        if not self.ask(0x06).startswith(b"FM1TRS"):
            sys.exit("the TRS tester answers, but not with FM1TRS")

    def send(self, cmd, payload=b""):
        self.out.send(mido.Message("sysex", data=PREFIX + [cmd] + pack7(payload)))

    def wait(self, reply, timeout):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            m = self.inp.poll()
            if m is None:
                time.sleep(0.0005)
                continue
            if m.type == "sysex" and list(m.data[:3]) == PREFIX and m.data[3] == reply:
                return unpack7(list(m.data[4:]))
        raise TimeoutError(f"no 0x{reply:02X} from the TRS tester in {timeout:.0f} s")

    def ask(self, cmd, payload=b"", timeout=2.0):
        self.send(cmd, payload)
        return self.wait(cmd | 0x40, timeout)

    def load(self, records):
        self.ask(0x01)
        blob = b"".join(encode_record(r) for r in records)
        if len(blob) > 96 * 1024:
            raise ValueError(f"script of {len(blob)} bytes, the tester holds 96 KB")
        for i in range(0, len(blob), 896):
            held = struct.unpack("<I", self.ask(0x02, blob[i:i + 896]))[0]
        if held != len(blob):
            raise RuntimeError(f"the tester holds {held} of {len(blob)} script bytes")

    def play(self):
        """returns the PC's time of the start"""
        self.send(0x03)
        self.wait(0x43, 2.0)
        return time.monotonic() - 0.0005                # half a USB frame back


def encode_record(r):
    at, what = r
    if isinstance(what, tuple):                          # ("break", us) or ("baud", 0.01 %)
        op = {"break": 1, "baud": 2}[what[0]]
        return struct.pack("<IBBH", at, 0, op, what[1] & 0xFFFF)
    data = bytes(what)
    assert 0 < len(data) < 256
    return struct.pack("<IB", at, len(data)) + data


# ------------------------------------------------------------------ what the FM-1 should send

def line_times(records):
    """(byte, time its stop bit ends, us) for every byte, with the line's queueing, breaks and
    baud changes, as the tester plays them"""
    out, free, byte_us = [], 0.0, BYTE_US
    for at, what in records:
        start = max(float(at), free)
        if isinstance(what, tuple):
            if what[0] == "break":
                free = start + what[1]
            else:
                byte_us = BYTE_US / (1 + what[1] / 10000.0)
                free = start
            continue
        for b in what:
            start += byte_us
            out.append((b, start))
        free = start
    return out


class Fm1Model:
    """app/midi/midi_msg.c's parser, then app/midi/merge.c's guard; emits the bytes the FM-1 puts
    out with the line time of the byte that completed them"""
    SX_CHUNK = 60

    def __init__(self):
        self.status = self.need = self.got = self.d1 = 0
        self.running = False
        self.in_sx = False
        self.sx = []
        self.held = [[0] * 128 for _ in range(16)]
        self.out = []

    def emit(self, data, t, channel=False):
        if channel and len(data) == 3:
            s, ch, note = data[0] & 0xF0, data[0] & 0x0F, data[1]
            if s == 0x90 and data[2]:
                if self.held[ch][note]:
                    self.out.append((bytes([0x80 | ch, note, 0]), t))
                self.held[ch][note] = min(self.held[ch][note] + 1, 255)
            elif s in (0x80, 0x90):
                if self.held[ch][note] > 1:
                    self.held[ch][note] -= 1
                    return
                self.held[ch][note] = 0
            elif s == 0xB0 and note in (120, 123):
                self.held[ch] = [0] * 128
        self.out.append((bytes(data), t))

    def sx_flush(self, t):
        if self.sx:
            self.emit(self.sx, t)
        self.sx = []

    def feed(self, b, t):
        if b >= 0xF8:
            if self.in_sx and self.sx:
                self.sx_flush(t)
            self.emit([b], t)
            return
        if b & 0x80:
            if self.in_sx:
                if b == 0xF7:
                    self.sx.append(b)
                self.sx_flush(t)
                self.in_sx = False
                if b == 0xF7:
                    return
            self.status = self.got = 0
            if b == 0xF0:
                self.in_sx = True
                self.sx = [0xF0]
                return
            if b == 0xF7:
                return
            self.need = 1 if (b < 0xF0 and (b & 0xE0) == 0xC0) or b in (0xF1, 0xF3) else \
                2 if b < 0xF0 or b == 0xF2 else 0
            if self.need == 0:
                self.emit([b], t)
                return
            self.status, self.running = b, b < 0xF0
            return
        if self.in_sx:
            if len(self.sx) == self.SX_CHUNK:
                self.sx_flush(t)
            self.sx.append(b)
            return
        if not self.status:
            return
        if self.need == 2 and self.got == 0:
            self.d1, self.got = b, 1
            return
        data = [self.status, self.d1, b] if self.need == 2 else [self.status, b]
        self.got = 0
        self.emit(data, t, channel=self.running)
        if not self.running:
            self.status = 0


def expected(records):
    m = Fm1Model()
    for b, t in line_times(records):
        m.feed(b, t)
    return m.out


# ------------------------------------------------------------------ both sides as messages

def parse_stream(chunks):
    """(bytes, time) chunks of a MIDI byte stream -> [(message bytes, time of its last byte)]:
    running status expanded, real time pulled out, SysEx whole, orphan data dropped"""
    msgs, status, need, data, sx = [], 0, 0, [], None
    for chunk, t in chunks:
        for b in chunk:
            if b >= 0xF8:
                msgs.append((bytes([b]), t))
                continue
            if b & 0x80:
                if sx is not None:
                    if b == 0xF7:
                        msgs.append((bytes(sx + [0xF7]), t))
                        sx = None
                        continue
                    msgs.append((bytes(sx), t))          # a SysEx cut short by a status
                    sx = None
                if b == 0xF0:
                    sx = [0xF0]
                    status = 0
                    continue
                if b == 0xF7:
                    continue
                need = 1 if (b < 0xF0 and (b & 0xE0) == 0xC0) or b in (0xF1, 0xF3) else \
                    2 if b < 0xF0 or b == 0xF2 else 0
                if need == 0:
                    msgs.append((bytes([b]), t))
                    status = 0
                    continue
                status, data = b, []
                continue
            if sx is not None:
                sx.append(b)
                continue
            if not status:
                continue
            data.append(b)
            if len(data) == need:
                msgs.append((bytes([status] + data), t))
                data = []
                if status >= 0xF0:
                    status = 0
    return msgs


class RawUsbReader:
    """the FM-1's USB MIDI as raw bytes, timestamped as they are read"""
    def __init__(self):
        card = None
        for line in open("/proc/asound/cards"):
            if line.strip()[:1].isdigit() and line.rstrip().endswith("- FM-1"):   # not "FM-1 TRS Tester"
                card = int(line.split()[0])
        if card is None:
            sys.exit("no FM-1 on USB")
        self.fd = os.open(f"/dev/snd/midiC{card}D0", os.O_RDONLY | os.O_NONBLOCK)
        self.chunks, self.run = [], True
        self.th = threading.Thread(target=self.loop, daemon=True)
        self.th.start()

    def loop(self):
        while self.run:
            try:
                b = os.read(self.fd, 4096)
            except BlockingIOError:
                time.sleep(0.0002)
                continue
            if b:
                self.chunks.append((b, time.monotonic()))

    def take(self):
        c, self.chunks = self.chunks, []
        return c

    def close(self):
        self.run = False
        self.th.join()
        os.close(self.fd)


def ble_stream(pkt):
    """a BLE MIDI packet -> the MIDI bytes in it (header and timestamps dropped; running status
    and SysEx continued across packets stay as they are on the wire)"""
    out, i, n = [], 1, len(pkt)
    while i < n:
        b = pkt[i]
        if b & 0x80:
            if i + 1 < n and pkt[i + 1] & 0x80:
                out.append(pkt[i + 1])                   # timestamp, status
                i += 2
            else:
                i += 1                                   # timestamp before running-status data
            continue
        out.append(b)
        i += 1
    return bytes(out)


# ------------------------------------------------------------------ the tests

def notes(n=200, step=15000, t0=0, chans=16):
    r, t = [], t0
    for k in range(n):
        ch, note = k % chans, 36 + (k * 7) % 60
        r.append((t, [0x90 | ch, note, 1 + k % 127]))
        r.append((t + 5000, [0x80 | ch, note, 0x40]))
        t += step
    return r


def clock(bpm, seconds, t0=0, start_stop=True):
    tick = 60e6 / (bpm * 24)
    r = [(t0, [0xFA])] if start_stop else []
    for k in range(int(seconds * 1e6 / tick)):
        r.append((int(t0 + k * tick), [0xF8]))
    if start_stop:
        r.append((int(t0 + seconds * 1e6), [0xFC]))
    return r


def merged(*lists):
    return sorted((x for l in lists for x in l), key=lambda r: r[0])


def t_notes():
    return notes(200, 15000)


def t_clock120():                                        # the open TRS item: notes under a clock
    return merged(clock(120, 12), notes(24, 500000, t0=100))


def t_clock300():
    return merged(clock(300, 8), notes(780, 10000, t0=100))


def t_saturate():                                        # 3 s of bytes back to back
    r = []
    for k in range(1040):
        ch, note = k % 16, 36 + (k * 5) % 60
        r += [(0, [0x90 | ch, note, 100]), (0, [0x80 | ch, note, 0]), (0, [0xB0 | ch, 1, k & 0x7F])]
    return r


def t_running():
    r, t = [], 0
    for k in range(60):                                  # note on/off pairs under one status each
        r.append((t, [0x91] + [36 + k % 40, 90, 36 + k % 40, 0]))
        r.append((t + 3000, [0xB2, 7, k, 10, 127 - k, 11, k]))
        r.append((t + 6000, [0xE3, k, 64, 0x7F - k, 64]))
        r.append((t + 9000, [0xC4, k, (k + 1) & 0x7F]))
        t += 20000
    return r


def t_realtime():                                        # real time in the middle of everything
    r, t = [], 0
    for k in range(80):
        n = 40 + k % 30
        r.append((t, [0x90, 0xF8, n, 0xFE, 100]))        # inside a note on
        r.append((t + 4000, [0x80, n, 0xF8, 0]))
        r.append((t + 8000, [0xF0, 0x7D, 1, 2, 0xF8, 3, 4, 0xFA, 5, 0xF7]))
        r.append((t + 14000, [0xB0, 74, 0xFC, k & 0x7F, 0xFB]))
        t += 20000
    return r


def t_sysex():
    small = [0xF0, 0x7D] + [i & 0x7F for i in range(1022)] + [0xF7]
    big = [0xF0, 0x7E, 0x7F] + [(i * 3) & 0x7F for i in range(4090)] + [0xF7]
    r = [(0, small[i:i + 200]) for i in range(0, len(small), 200)]   # records hold 255 bytes at most
    r += [(1, big[i:i + 200]) for i in range(0, len(big), 200)]
    r += notes(20, 10000, t0=1)
    return [(at, list(b)) for at, b in r]


def t_garbage():
    seq = [
        [0x12, 0x34, 0x56],                              # orphan data before any status
        [0x90, 0x3C, 0x64],                              # a good note
        [0x90, 0x3D],                                    # cut short by the next status
        [0xB0, 0x07, 0x64],
        [0xF4], [0xF5], [0xF9], [0xFD],                  # undefined: the FM-1 passes them
        [0xF7],                                          # a stray end of SysEx
        [0xF0, 0x7D, 0x01, 0x02],                        # a SysEx cut short by a status
        [0x80, 0x3C, 0x00],
        [0xF2, 0x10, 0x20], [0xF3, 0x05], [0xF1, 0x31], [0xF6],   # system common
        [0x22, 0x33],                                    # orphan again: system common has no running status
        [0xC0, 0x05, 0x06],                              # program changes under running status
        [0xFF],                                          # system reset (real time)
    ]
    r, t = [(0, [0xF6])], 2000                         # tune request: no running status left from the test before
    for rep in range(10):
        for s in seq:
            r.append((t, s))
            t += 2000
    return r


def t_baud(offsets=(-400, -300, -200, -100, 100, 200, 300, 400)):
    r, t = [], 0
    for off in offsets:
        r.append((t, ("baud", off)))
        r += notes(25, 8000, t0=t + 1000)
        t += 25 * 8000 + 20000
        r.append((t, ("baud", 0)))
        t += 5000
    return r


def t_break():
    r, t = [], 0
    for k in range(20):
        r.append((t, [0x90, 40 + k, 100]))
        r.append((t + 3000, ("break", 10000)))           # a framing error mid running status
        r.append((t + 20000, [40 + k, 0]))                # running status note off after it
        r.append((t + 25000, [0xB0, 1, k]))
        t += 40000
    return r


TESTS = {"notes": t_notes, "clock120": t_clock120, "clock300": t_clock300, "saturate": t_saturate,
         "running": t_running, "realtime": t_realtime, "sysex": t_sysex, "garbage": t_garbage,
         "baud": t_baud, "break": t_break}


# ------------------------------------------------------------------ running one

def compare(name, exp, got, t_play, label):
    want = [m for m, _ in exp]
    have = [m for m, _ in got]
    missing, extra = list(want), []
    for m in have:
        if m in missing:
            missing.remove(m)
        else:
            extra.append(m)
    ok = want == have
    print(f"  {label}: expected {len(want)}  received {len(have)}  missing {len(missing)}  "
          f"extra {len(extra)}  in order {ok}")
    for tag, l in (("missing", missing), ("extra", extra)):
        if l:
            print(f"    first {tag}: " + "  ".join(m.hex(" ") if len(m) < 12 else m[:8].hex(" ") + f" .. ({len(m)} B)"
                                                for m in l[:6]))
    lat, j = [], 0
    for m, t in exp:                                     # each expected message against the next equal one
        for k in range(j, len(got)):
            if got[k][0] == m:
                lat.append((got[k][1] - (t_play + t / 1e6)) * 1000)
                j = k + 1
                break
    if lat:
        s = sorted(lat)
        print(f"    latency ms: min {s[0]:.1f}  median {s[len(s) // 2]:.1f}  p95 {s[int(len(s) * .95)]:.1f}  max {s[-1]:.1f}")
    clk = [t for m, t in got if m == b"\xf8"]
    if len(clk) > 10:
        g = [(b - a) * 1000 for a, b in zip(clk, clk[1:])]
        mean = sum(g) / len(g)
        sd = (sum((x - mean) ** 2 for x in g) / len(g)) ** 0.5
        print(f"    clock spacing ms: mean {mean:.2f}  sd {sd:.2f}  min {min(g):.1f}  max {max(g):.1f}")
    return ok


def fm1_status():
    import subprocess
    r = subprocess.run([sys.executable, os.path.join(HERE, "fm1_editor.py"), "status"],
                       capture_output=True, text=True, timeout=20)
    return r.stdout.strip()


def run_test(name, tester, ble):
    records = TESTS[name]()
    exp = expected(records)
    dur = (line_times(records)[-1][1] if records else 0) / 1e6
    print(f"== {name}: {len(records)} records, {len(exp)} messages expected, {dur:.1f} s on the line")
    tester.load(records)
    usb = RawUsbReader()
    if ble:
        ble.take()
    time.sleep(0.3)
    usb.take()
    t_play = tester.play()
    stats = struct.unpack("<4I", tester.wait(0x50, dur + 10))   # the end of play
    time.sleep(1.0)                                      # the tail through the FM-1
    usb_chunks = usb.take()
    usb.close()
    print(f"  tester: {stats[0]} records, {stats[1]} bytes, at most {stats[2]} bytes queued, "
          f"{stats[3] / 1000:.1f} ms late at worst")
    exp_msgs = parse_stream([(m, t) for m, t in exp])
    ok = compare(name, exp_msgs, parse_stream(usb_chunks), t_play, "USB")
    if ble:
        ok &= compare(name, exp_msgs, parse_stream([(ble_stream(p), t) for p, t in ble.take()]), t_play, "BLE")
    return ok


class Ble:
    """the FM-1's BLE MIDI notifications, the PC as the central (usb_to_ble.py's connection)"""
    def __init__(self, addr):
        import dbus
        import dbus.mainloop.glib
        from gi.repository import GLib
        sys.path.insert(0, HERE)
        import usb_to_ble as u
        dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
        self.dbus, self.GLib = dbus, GLib
        bus = dbus.SystemBus()
        om = dbus.Interface(bus.get_object(u.BLUEZ, "/"), u.OM_IFACE)
        adapter = next(p for p, i in om.GetManagedObjects().items() if "org.bluez.Adapter1" in i)
        dev_path = u.find_device(bus, adapter, addr.upper() if addr else None, "FM-1 MIDI")
        self.dev = dbus.Interface(bus.get_object(u.BLUEZ, dev_path), u.DEVICE_IFACE)
        props = dbus.Interface(bus.get_object(u.BLUEZ, dev_path), u.PROPS_IFACE)
        self.dev.Connect()
        for _ in range(100):
            if props.Get(u.DEVICE_IFACE, "ServicesResolved"):
                break
            time.sleep(0.1)
        char = next(p for p, i in om.GetManagedObjects().items()
                    if p.startswith(dev_path) and u.GATT_CHRC in i and str(i[u.GATT_CHRC]["UUID"]).lower() == u.MIDI_CHAR)
        self.pkts = []
        bus.add_signal_receiver(lambda iface, ch, inv: "Value" in ch and self.pkts.append((bytes(ch["Value"]), time.monotonic())),
                                "PropertiesChanged", u.PROPS_IFACE, path=char)
        dbus.Interface(bus.get_object(u.BLUEZ, char), u.GATT_CHRC).StartNotify()
        self.loop = GLib.MainLoop()

    def take(self):
        p, self.pkts = self.pkts, []
        return p

    def close(self):
        self.dev.Disconnect()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("tests", nargs="*", metavar="TEST")
    ap.add_argument("--ble", action="store_true", help="check the BLE side too (the PC as the central)")
    ap.add_argument("--addr", help="the FM-1's BLE address (default: found by name)")
    ap.add_argument("--list", action="store_true")
    args = ap.parse_args()
    if args.list:
        print(" ".join(TESTS))
        return
    names = args.tests or list(TESTS)
    for n in names:
        if n not in TESTS:
            sys.exit(f"no test {n}: " + ", ".join(TESTS))
    tester = Tester()
    before = fm1_status()
    ble = Ble(args.addr) if args.ble else None
    results = {}

    def work():
        try:
            for n in names:
                results[n] = run_test(n, tester, ble)
        finally:
            if ble:
                ble.GLib.idle_add(ble.loop.quit)

    if ble:
        th = threading.Thread(target=work, daemon=True)
        th.start()
        ble.loop.run()
        th.join()
        ble.close()
    else:
        work()
    print("FM-1 before:", before)
    print("FM-1 after: ", fm1_status())
    print("results: " + "  ".join(f"{n} {'ok' if v else 'FAIL'}" for n, v in results.items()))
    sys.exit(0 if results and all(results.values()) else 1)


if __name__ == "__main__":
    main()
