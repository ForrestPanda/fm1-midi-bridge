#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""The USB to BLE route under test, with the PC as the BLE MIDI central (BlueZ over D-Bus):
scan for the FM-1, connect, subscribe to the MIDI characteristic, send a known sequence into the
FM-1's USB MIDI port (per group: note on, note off, CC 74), and check that every message comes
back over BLE, in order. Prints the loss, the latency and the packets the link carried; exits 1
when a message is missing or out of order. --reverse runs BLE to USB the same way: the PC writes
the sequence to the characteristic (write without response, as a keyboard does) and reads the
FM-1's USB port.

  tools/usb_to_ble.py [--addr ADDR] [--name "FM-1 MIDI"] [--count 40] [--gap MS] [--clock BPM] [--reverse] [--port NAME]
  --count  groups of three messages; --gap 0 sends the whole sequence as one burst (with
           --reverse: packed into packets of the link's size, else one group per packet)
  --clock  a MIDI clock (F8, 24 per quarter) at BPM under the notes, from a second before them to
           their end: the notes are checked as without it, the clock for count and spacing

Needs python3-dbus, python3-gi, mido with python-rtmidi; the FM-1 in peripheral mode (not host
mode) with nothing else connected to it over BLE; the USB to BLE route on. The link is dropped
at the end, so a phone can connect again.

First unit, 2026-10-20: steady rates up to 600 messages/s arrive whole (latency median 7 ms);
bursts of 600 messages at once arrive whole with the BLE queue in `app/bridge.c` (192 of 600
before it); a burst of 1200 lost 2%, on the USB side. --clock 120, 200, 300 under notes: no loss, latency
median 5-7 ms. --reverse: steady rates and bursts of 300 whole. A run after ~45 minutes of soak against
the PC's AX200 measured the adapter: power it off and on first (bluetoothctl power off; power on)."""
import argparse
import sys
import threading
import time

import dbus
import dbus.mainloop.glib
from gi.repository import GLib
import mido

BLUEZ = "org.bluez"
OM_IFACE = "org.freedesktop.DBus.ObjectManager"
PROPS_IFACE = "org.freedesktop.DBus.Properties"
DEVICE_IFACE = "org.bluez.Device1"
GATT_CHRC = "org.bluez.GattCharacteristic1"
MIDI_CHAR = "7772e5db-3868-4112-a1a9-f2669d106bf3"
DATA_LEN = {0x80: 2, 0x90: 2, 0xA0: 2, 0xB0: 2, 0xC0: 1, 0xD0: 1, 0xE0: 2}


def decode(pkt):
    """BLE MIDI packet -> the channel and real-time messages in it, as bytes (timestamps and
    running status resolved; the sequence this tool sends has no SysEx).
    Each comes with the sender's 13-bit millisecond timestamp."""
    out, i, status, n = [], 1, None, len(pkt)
    high, low = (pkt[0] & 0x3F) << 7 if n else 0, None
    ts = 0
    while i < n:
        if pkt[i] & 0x80:
            if low is not None and (pkt[i] & 0x7F) < low:
                high += 0x80                         # the low 7 bits wrapped within the packet
            low = pkt[i] & 0x7F
            ts = (high | low) & 0x1FFF
        if pkt[i] & 0x80 and i + 1 < n and pkt[i + 1] & 0x80:
            s = pkt[i + 1]                           # timestamp, status
            i += 2
            if s >= 0xF8:                            # real time: one byte, running status kept
                out.append((ts, bytes([s])))
                continue
            status = s
        elif pkt[i] & 0x80:
            i += 1                                   # timestamp, running-status data
        need = DATA_LEN.get(status & 0xF0, 0) if status else 0
        if not need or i + need > n:
            break
        out.append((ts, bytes([status]) + bytes(pkt[i:i + need])))
        i += need
    return out


def find_device(bus, adapter_path, addr, name, timeout=15.0):
    """scan (LE) until the device shows up; BlueZ forgets an unpaired one when a scan ends"""
    om = dbus.Interface(bus.get_object(BLUEZ, "/"), OM_IFACE)
    adapter = dbus.Interface(bus.get_object(BLUEZ, adapter_path), "org.bluez.Adapter1")
    adapter.SetDiscoveryFilter({"Transport": "le"})
    adapter.StartDiscovery()
    try:
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            for path, ifaces in om.GetManagedObjects().items():
                d = ifaces.get(DEVICE_IFACE)
                if d and (str(d.get("Address", "")).upper() == addr if addr else str(d.get("Name", "")) == name):
                    return path
            time.sleep(0.1)
    finally:
        adapter.StopDiscovery()
    sys.exit(f"{addr or name!r} not seen in {timeout:.0f} s of scanning")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--addr", help="the FM-1's BLE address (default: found by --name)")
    ap.add_argument("--name", default="FM-1 MIDI")
    ap.add_argument("--count", type=int, default=40, help="groups of note on, note off, CC")
    ap.add_argument("--gap", type=float, default=50.0, help="ms between groups (0: one burst)")
    ap.add_argument("--clock", type=float, default=0, metavar="BPM", help="a MIDI clock under the notes")
    ap.add_argument("--reverse", action="store_true", help="BLE to USB: the PC writes, the USB port reads")
    ap.add_argument("--port", help="the FM-1's USB MIDI port (default: the first starting with FM-1)")
    args = ap.parse_args()

    dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
    bus = dbus.SystemBus()
    om = dbus.Interface(bus.get_object(BLUEZ, "/"), OM_IFACE)
    adapter_path = next((p for p, i in om.GetManagedObjects().items() if "org.bluez.Adapter1" in i), None)
    if not adapter_path:
        sys.exit("no Bluetooth adapter (is bluetoothd running?)")
    names = mido.get_input_names() if args.reverse else mido.get_output_names()
    port = args.port or next((n for n in names if n.startswith("FM-1")), None)
    if not port:
        sys.exit("no FM-1 USB MIDI port")

    dev_path = find_device(bus, adapter_path, args.addr.upper() if args.addr else None, args.name)
    dev = dbus.Interface(bus.get_object(BLUEZ, dev_path), DEVICE_IFACE)
    props = dbus.Interface(bus.get_object(BLUEZ, dev_path), PROPS_IFACE)
    print("connecting", dev_path.rsplit("dev_", 1)[1].replace("_", ":"))
    try:
        dev.Connect()
    except dbus.DBusException as e:
        sys.exit("connect: " + e.get_dbus_message())
    for _ in range(100):
        if props.Get(DEVICE_IFACE, "ServicesResolved"):
            break
        time.sleep(0.1)
    else:
        dev.Disconnect()
        sys.exit("services not resolved in 10 s")
    char_path = next((p for p, i in om.GetManagedObjects().items()
                      if p.startswith(dev_path) and GATT_CHRC in i and str(i[GATT_CHRC]["UUID"]).lower() == MIDI_CHAR), None)
    if not char_path:
        dev.Disconnect()
        sys.exit("no MIDI characteristic (is the FM-1 in host mode?)")

    pkts, got, sent, clock_ts = [], [], [], []                     # (time, length), (time, message) x 2

    def on_props(iface, changed, invalidated):
        if "Value" in changed:
            pkt = bytes(changed["Value"])
            t = time.monotonic()
            pkts.append((t, len(pkt)))
            for ts, m in decode(pkt):
                got.append((t, m))
                if m == b"\xf8":
                    clock_ts.append(ts)

    chrc = dbus.Interface(bus.get_object(BLUEZ, char_path), GATT_CHRC)
    if not args.reverse:
        bus.add_signal_receiver(on_props, "PropertiesChanged", PROPS_IFACE, path=char_path)
        chrc.StartNotify()
    loop = GLib.MainLoop()

    clocks = []                                      # send times of the F8s
    try:                                             # BlueZ 5.62+ tells the ATT MTU
        max_pkt = int(dbus.Interface(bus.get_object(BLUEZ, char_path), PROPS_IFACE).Get(GATT_CHRC, "MTU")) - 3
    except dbus.DBusException:
        max_pkt = 20
    pending = bytearray()                            # --reverse: the BLE packet being filled
    write_errors = []

    def ble_put(m):                                  # one message, its own timestamp, full status
        ts = int(time.monotonic() * 1000) & 0x1FFF
        if len(pending) + 1 + len(m) > max_pkt:
            ble_flush()
        if not pending:
            pending.append(0x80 | (ts >> 7))
        pending.append(0x80 | (ts & 0x7F))
        pending.extend(m)

    def ble_flush():
        if pending:
            try:
                chrc.WriteValue(dbus.Array(bytes(pending), signature="y"), {"type": "command"})
            except dbus.DBusException as e:
                write_errors.append(e.get_dbus_message())
            pending.clear()

    class BleOut:                                    # --reverse: the sink in the USB port's place
        def send(self, msg):
            ble_put(bytes(msg.bytes()))
            if args.gap or msg.type == "clock":
                if msg.type == "clock" or msg.type == "control_change":   # the end of a group
                    ble_flush()

    def on_usb(msg):                                 # --reverse: what the FM-1 sends to the PC
        t = time.monotonic()
        if msg.type in ("note_on", "note_off", "control_change", "clock"):
            got.append((t, bytes(msg.bytes())))

    def sender():
        time.sleep(1.0)                              # the subscription settles
        tick = 60.0 / (args.clock * 24) if args.clock else None
        if args.reverse:
            inp = mido.open_input(port, callback=on_usb)
            ctx, out = inp, BleOut()
        else:
            ctx = out = mido.open_output(port)
        with ctx:
            t = time.monotonic()
            next_clock = t
            next_group = t + (1.0 if tick else 0.0)
            k = 0
            while k < args.count:                    # one loop: rtmidi ports are not for two threads
                now = time.monotonic()
                if tick and now >= next_clock:
                    out.send(mido.Message("clock"))
                    clocks.append(now)
                    next_clock += tick
                    continue
                if now >= next_group:
                    note = 48 + k % 24
                    for msg in (mido.Message("note_on", channel=k % 16, note=note, velocity=1 + k % 127),
                                mido.Message("note_off", channel=k % 16, note=note, velocity=0),
                                mido.Message("control_change", channel=0, control=74, value=k % 128)):
                        out.send(msg)
                        sent.append((time.monotonic(), bytes(msg.bytes())))
                    k += 1
                    next_group += args.gap / 1000
                    continue
                due = min(next_group, next_clock) if tick else next_group
                if due > now:
                    time.sleep(min(due - now, 0.002))
            if args.reverse:
                ble_flush()
            time.sleep(2.0)                          # the tail of a burst
        if not args.reverse:
            GLib.idle_add(loop.quit)

    if args.reverse:                                 # D-Bus writes from this thread, no main loop needed
        sender()
    else:
        threading.Thread(target=sender, daemon=True).start()
        loop.run()
        try:
            chrc.StopNotify()
        except dbus.DBusException:
            pass
    dev.Disconnect()
    if write_errors:
        print(f"writes refused by BlueZ: {len(write_errors)} ({write_errors[0]})")

    tx, rx = [m for _, m in sent], [m for _, m in got if m != b"\xf8"]
    rx_clock = [t for t, m in got if m == b"\xf8"]
    missing, extra = list(tx), []
    for m in rx:
        if m in missing:
            missing.remove(m)
        else:
            extra.append(m)
    print(f"sent {len(tx)}  received {len(rx)}  missing {len(missing)}  extra {len(extra)}  in order {rx == tx}")
    if missing:
        print("  first missing:", " ".join(m.hex() for m in missing[:8]))
    if extra:
        print("  first extra:", " ".join(m.hex() for m in extra[:8]))
    lat, used = [], [False] * len(got)               # each sent message against the first equal one received after it
    for ts, m in sent:
        for i, (tr, r) in enumerate(got):
            if not used[i] and r == m and tr >= ts - 0.005:
                used[i] = True
                lat.append((tr - ts) * 1000)
                break
    if lat:
        lat.sort()
        print(f"latency ms: min {lat[0]:.1f}  median {lat[len(lat) // 2]:.1f}  "
              f"p95 {lat[int(len(lat) * .95)]:.1f}  max {lat[-1]:.1f}")
    if clocks:
        tick_ms = 60000.0 / (args.clock * 24)
        print(f"clock {args.clock:g} bpm: sent {len(clocks)}  received {len(rx_clock)}", end="")
        if len(rx_clock) > 2:
            gaps = sorted((b - a) * 1000 for a, b in zip(rx_clock, rx_clock[1:]))
            mean = sum(gaps) / len(gaps)
            dev = (sum((g - mean) ** 2 for g in gaps) / len(gaps)) ** 0.5
            print(f"\n  arrival spacing ms (sent every {tick_ms:.2f}): mean {mean:.2f}  sd {dev:.2f}  "
                  f"min {gaps[0]:.1f}  max {gaps[-1]:.1f}", end="")
        if len(clock_ts) > 2:                        # the FM-1's own timestamps, what a receiver should play by
            tg = sorted((b - a) % 8192 for a, b in zip(clock_ts, clock_ts[1:]))
            tmean = sum(tg) / len(tg)
            tdev = (sum((g - tmean) ** 2 for g in tg) / len(tg)) ** 0.5
            print(f"\n  timestamp spacing ms: mean {tmean:.2f}  sd {tdev:.2f}  min {tg[0]}  max {tg[-1]}", end="")
        print()
    if args.reverse:
        print(f"packets of up to {max_pkt} bytes")
    if len(pkts) > 1:
        span = pkts[-1][0] - pkts[0][0]
        total = sum(n for _, n in pkts)
        print(f"packets {len(pkts)}  bytes/packet max {max(n for _, n in pkts)} mean {total / len(pkts):.1f}  "
              f"throughput {total / max(span, 1e-3):.0f} B/s")
    sys.exit(0 if rx == tx and len(rx_clock) == len(clocks) else 1)


if __name__ == "__main__":
    main()
