#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""A BLE MIDI keyboard made of the PC's Bluetooth adapter (BlueZ over D-Bus): advertises the
MIDI service under a name, takes one central, and once it subscribes plays a note every so
often; whatever the central writes is decoded and printed. The test keyboard for the FM-1's
host mode when no real one is on the desk.

  tools/ble_midi_peripheral.py [--name "PC MIDI"] [--period 1.0] [--note 60] [--channel 1] [--quiet]
  Ctrl-C stops it; the advertisement, the service and the pairing agent are unregistered on exit.

Needs python3-dbus and python3-gi, bluetoothd running with the adapter powered. The PC cannot
be both ends of one link: disconnect it from "FM-1 MIDI" first (bluetoothctl disconnect). The
agent answers every pairing request with yes (no input, no output), so "Pair on connect" on the
FM-1 can be tested too; the bond stays in BlueZ until `bluetoothctl remove ADDR`.

Seen on 2026-10-19: the first run put the name in the scan response, later runs (after the
script had been killed and restarted) left the scan response empty, so the FM-1 lists the
adapter's address and, once connected, reads the GAP device name, which is the adapter's alias
("silver"), not NAME. Stopping the script does not drop the link, bluetoothd keeps it: use
`bluetoothctl disconnect <the FM-1's address>` to test a drop. Stop the script with Ctrl-C or
SIGTERM (`pkill -f "ble_midi_peripheral.py -[-]name"`; a background job ignores SIGINT), so
that it unregisters."""
import argparse
import signal
import sys
import time

import dbus
import dbus.mainloop.glib
import dbus.service
from gi.repository import GLib

BLUEZ = "org.bluez"
ADAPTER_IFACE = "org.bluez.Adapter1"
DEVICE_IFACE = "org.bluez.Device1"
GATT_MANAGER = "org.bluez.GattManager1"
GATT_SERVICE = "org.bluez.GattService1"
GATT_CHRC = "org.bluez.GattCharacteristic1"
ADV_MANAGER = "org.bluez.LEAdvertisingManager1"
ADV_IFACE = "org.bluez.LEAdvertisement1"
AGENT_MANAGER = "org.bluez.AgentManager1"
AGENT_IFACE = "org.bluez.Agent1"
OM_IFACE = "org.freedesktop.DBus.ObjectManager"
PROPS_IFACE = "org.freedesktop.DBus.Properties"

MIDI_SERVICE = "03b80e5a-ede8-4b33-a751-6ce34ec4c700"
MIDI_IO = "7772e5db-3868-4112-a1a9-f2669d106bf3"
BASE = "/fm1/blemidi"


def now():
    return time.strftime("%H:%M:%S") + f".{int(time.time() * 1000) % 1000:03d}"


def say(msg):
    print(f"{now()} {msg}", flush=True)


def decode_ble_midi(pkt):
    """BLE MIDI 1.0: header, then (timestamp, status, data...) groups; running status allowed.
    Returns a list of readable messages."""
    out, i, status = [], 1, None
    while i < len(pkt):
        b = pkt[i]
        if b & 0x80:                                   # a timestamp low byte, a status follows (or data in running status)
            i += 1
            if i < len(pkt) and pkt[i] & 0x80:
                status = pkt[i]
                i += 1
                if status == 0xF0:
                    j = i
                    while j < len(pkt) and not pkt[j] & 0x80:
                        j += 1
                    out.append("SysEx " + " ".join(f"{x:02X}" for x in pkt[i:j]))
                    i = j
                    continue
                if status >= 0xF8:
                    out.append({0xF8: "clock", 0xFA: "start", 0xFB: "continue", 0xFC: "stop", 0xFE: "active sense", 0xFF: "reset"}.get(status, f"{status:02X}"))
                    continue
                if status == 0xF7:
                    out.append("SysEx end")
                    continue
        if status is None:
            out.append(f"?{b:02X}")
            i += 1
            continue
        n = 1 if (status & 0xF0) in (0xC0, 0xD0) or status in (0xF1, 0xF3) else 2
        data = pkt[i:i + n]
        i += n
        kind = status & 0xF0
        ch = (status & 0x0F) + 1
        if kind == 0x90 and len(data) == 2 and data[1]:
            out.append(f"note on ch{ch} {data[0]} vel {data[1]}")
        elif kind in (0x80, 0x90) and len(data) == 2:
            out.append(f"note off ch{ch} {data[0]}")
        elif kind == 0xB0 and len(data) == 2:
            out.append(f"CC ch{ch} {data[0]} = {data[1]}")
        elif kind == 0xC0:
            out.append(f"program ch{ch} {data[0]}")
        elif kind == 0xE0 and len(data) == 2:
            out.append(f"bend ch{ch} {data[0] | data[1] << 7}")
        else:
            out.append(f"{status:02X} " + " ".join(f"{x:02X}" for x in data))
    return out


class Application(dbus.service.Object):
    def __init__(self, bus, chrc_factory):
        self.path = BASE + "/app"
        dbus.service.Object.__init__(self, bus, self.path)
        self.service = MidiService(bus, 0, chrc_factory)

    @dbus.service.method(OM_IFACE, out_signature="a{oa{sa{sv}}}")
    def GetManagedObjects(self):
        objs = {self.service.path: self.service.properties()}
        for c in self.service.chrcs:
            objs[c.path] = c.properties()
        return objs


class MidiService(dbus.service.Object):
    def __init__(self, bus, index, chrc_factory):
        self.path = f"{BASE}/service{index}"
        dbus.service.Object.__init__(self, bus, self.path)
        self.chrcs = [chrc_factory(bus, self)]

    def properties(self):
        return {GATT_SERVICE: {"UUID": MIDI_SERVICE, "Primary": True,
                               "Characteristics": dbus.Array([c.path for c in self.chrcs], signature="o")}}


class MidiCharacteristic(dbus.service.Object):
    def __init__(self, bus, service):
        self.path = service.path + "/char0"
        self.service = service
        self.notifying = False
        self.on_write = None
        dbus.service.Object.__init__(self, bus, self.path)

    def properties(self):
        return {GATT_CHRC: {"Service": dbus.ObjectPath(self.service.path), "UUID": MIDI_IO,
                            "Flags": dbus.Array(["read", "write-without-response", "write", "notify"], signature="s")}}

    @dbus.service.method(PROPS_IFACE, in_signature="s", out_signature="a{sv}")
    def GetAll(self, iface):
        return self.properties()[iface]

    @dbus.service.method(GATT_CHRC, in_signature="a{sv}", out_signature="ay")
    def ReadValue(self, options):
        return dbus.Array([], signature="y")             # the MIDI I/O characteristic reads as empty

    @dbus.service.method(GATT_CHRC, in_signature="aya{sv}")
    def WriteValue(self, value, options):
        pkt = bytes(value)
        who = options.get("device", "?")
        say(f"<- {' '.join(f'{b:02X}' for b in pkt)}   {'; '.join(decode_ble_midi(pkt))}   (from {str(who).rsplit('/', 1)[-1]})")
        if self.on_write:
            self.on_write(pkt)

    @dbus.service.method(GATT_CHRC)
    def StartNotify(self):
        self.notifying = True
        say("central subscribed (CCCD notify on)")

    @dbus.service.method(GATT_CHRC)
    def StopNotify(self):
        self.notifying = False
        say("central unsubscribed")

    @dbus.service.signal(PROPS_IFACE, signature="sa{sv}as")
    def PropertiesChanged(self, iface, changed, invalidated):
        pass

    def notify(self, pkt):
        if not self.notifying:
            return False
        self.PropertiesChanged(GATT_CHRC, {"Value": dbus.Array(pkt, signature="y")}, [])
        return True


class Advertisement(dbus.service.Object):
    def __init__(self, bus, name):
        self.path = BASE + "/adv0"
        self.name = name
        dbus.service.Object.__init__(self, bus, self.path)

    def properties(self):
        return {"Type": "peripheral", "ServiceUUIDs": dbus.Array([MIDI_SERVICE], signature="s"),
                "LocalName": self.name, "Discoverable": dbus.Boolean(True)}

    @dbus.service.method(PROPS_IFACE, in_signature="s", out_signature="a{sv}")
    def GetAll(self, iface):
        return self.properties()

    @dbus.service.method(ADV_IFACE)
    def Release(self):
        say("advertisement released by bluetoothd")


class Agent(dbus.service.Object):
    """says yes to everything: a keyboard with no buttons for pairing"""
    def __init__(self, bus):
        self.path = BASE + "/agent"
        dbus.service.Object.__init__(self, bus, self.path)

    @dbus.service.method(AGENT_IFACE)
    def Release(self):
        pass

    @dbus.service.method(AGENT_IFACE, in_signature="o", out_signature="s")
    def RequestPinCode(self, device):
        say(f"pairing: pin code asked by {device}, answering 0000")
        return "0000"

    @dbus.service.method(AGENT_IFACE, in_signature="o", out_signature="u")
    def RequestPasskey(self, device):
        say(f"pairing: passkey asked by {device}, answering 0")
        return dbus.UInt32(0)

    @dbus.service.method(AGENT_IFACE, in_signature="ouq")
    def DisplayPasskey(self, device, passkey, entered):
        say(f"pairing: passkey {passkey:06d} for {device}")

    @dbus.service.method(AGENT_IFACE, in_signature="os")
    def DisplayPinCode(self, device, pincode):
        say(f"pairing: pin {pincode} for {device}")

    @dbus.service.method(AGENT_IFACE, in_signature="ou")
    def RequestConfirmation(self, device, passkey):
        say(f"pairing: confirmation {passkey:06d} for {device}: yes")

    @dbus.service.method(AGENT_IFACE, in_signature="o")
    def RequestAuthorization(self, device):
        say(f"pairing: authorization for {device}: yes")

    @dbus.service.method(AGENT_IFACE, in_signature="os")
    def AuthorizeService(self, device, uuid):
        say(f"pairing: service {uuid} for {device}: yes")

    @dbus.service.method(AGENT_IFACE)
    def Cancel(self):
        say("pairing: cancelled")


def find_adapter(bus):
    om = dbus.Interface(bus.get_object(BLUEZ, "/"), OM_IFACE)
    for path, ifaces in om.GetManagedObjects().items():
        if GATT_MANAGER in ifaces and ADV_MANAGER in ifaces:
            return path
    sys.exit("no Bluetooth adapter with GATT server and advertising support (is bluetoothd running?)")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--name", default="PC MIDI")
    ap.add_argument("--period", type=float, default=1.0, help="seconds between notes (0 = none)")
    ap.add_argument("--note", type=int, default=60)
    ap.add_argument("--channel", type=int, default=1)
    ap.add_argument("--quiet", action="store_true", help="do not print each note sent")
    a = ap.parse_args()

    dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
    bus = dbus.SystemBus()
    adapter = find_adapter(bus)
    props = dbus.Interface(bus.get_object(BLUEZ, adapter), PROPS_IFACE)
    if not props.Get(ADAPTER_IFACE, "Powered"):
        props.Set(ADAPTER_IFACE, "Powered", dbus.Boolean(True))
    say(f"adapter {adapter} {props.Get(ADAPTER_IFACE, 'Address')} powered")

    app = Application(bus, MidiCharacteristic)
    chrc = app.service.chrcs[0]
    adv = Advertisement(bus, a.name)
    agent = Agent(bus)
    loop = GLib.MainLoop()

    def on_props(iface, changed, invalidated=None, path=None):   # some senders leave out the invalidated list
        if iface != DEVICE_IFACE:
            return
        if "Connected" in changed:
            try:
                dev = dbus.Interface(bus.get_object(BLUEZ, path), PROPS_IFACE)
                who = f"{dev.Get(DEVICE_IFACE, 'Address')} {dev.Get(DEVICE_IFACE, 'Alias')}"
            except dbus.DBusException:
                who = path
            say(("connected: " if changed["Connected"] else "disconnected: ") + who)
        if changed.get("Paired"):
            say(f"paired: {path}")
    bus.add_signal_receiver(on_props, dbus_interface=PROPS_IFACE, signal_name="PropertiesChanged", path_keyword="path")

    def ts():
        return int(time.monotonic() * 1000) & 0x1FFF

    def packet(*midi):
        t = ts()
        return bytes([0x80 | (t >> 7), 0x80 | (t & 0x7F)] + list(midi))

    def note_off():
        if chrc.notify(packet(0x80 | (a.channel - 1), a.note, 0)) and not a.quiet:
            say(f"-> note off {a.note}")
        return False

    def note_on():
        if chrc.notify(packet(0x90 | (a.channel - 1), a.note, 100)):
            if not a.quiet:
                say(f"-> note on {a.note}")
            GLib.timeout_add(min(250, int(a.period * 500)), note_off)
        return True

    if a.period > 0:
        GLib.timeout_add(int(a.period * 1000), note_on)

    gm = dbus.Interface(bus.get_object(BLUEZ, adapter), GATT_MANAGER)
    am = dbus.Interface(bus.get_object(BLUEZ, adapter), ADV_MANAGER)
    agm = dbus.Interface(bus.get_object(BLUEZ, "/org/bluez"), AGENT_MANAGER)
    gm.RegisterApplication(app.path, {}, reply_handler=lambda: say("MIDI service registered"),
                           error_handler=lambda e: (say(f"service registration failed: {e}"), loop.quit()))
    am.RegisterAdvertisement(adv.path, {}, reply_handler=lambda: say(f'advertising "{a.name}" with the MIDI service'),
                             error_handler=lambda e: (say(f"advertising failed: {e}"), loop.quit()))
    try:
        agm.RegisterAgent(agent.path, "NoInputNoOutput")
        agm.RequestDefaultAgent(agent.path)
    except dbus.DBusException as e:
        say(f"pairing agent not registered ({e}); pairing requests will go to the desktop's agent")
    GLib.unix_signal_add(GLib.PRIORITY_HIGH, signal.SIGTERM, lambda *_: (loop.quit(), False)[1])
    try:
        loop.run()
    except KeyboardInterrupt:
        pass
    finally:
        for f in (lambda: am.UnregisterAdvertisement(adv.path), lambda: gm.UnregisterApplication(app.path),
                  lambda: agm.UnregisterAgent(agent.path)):
            try:
                f()
            except dbus.DBusException:
                pass
        say("stopped")


if __name__ == "__main__":
    main()
