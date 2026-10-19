#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Walk every UI page of the bridge firmware in fm1-emu's live tool, one key script per
page, and report any halt (an instruction the emulator cannot decode shows up as a halt
naming the class and the pc; the image-wide instruction check cannot replace this, it
loses alignment on this image).

    tools/ui_walk.py [--emu DIR] [--fwsc FILE] [--out DIR] [--jobs N] [--only NAME]

Each run is `fm1-live --fwsc ... --shot MS` with FM1_BENCH_KEYS (its script syntax:
`ms:id` press, `ms:id^` release, `ms:eN:clicks` encoder, `ms:mHEX` MIDI into the jack),
from its own directory under --out (work/panel.ppm is written there; panel.png is the LCD
cropped out of it when ImageMagick is installed). Besides the picture at the end of the
script, a run takes pictures of its sub-pages at the times listed with it, by rerunning the
same script with an earlier --shot: look at them when the firmware's menus change, a
script that lands on the wrong row exercises nothing.

What the scripts rely on: the splash lasts 1500 ms after boot and the Status
page (the home page) is up from then on; OCT+ or HOME there opens the main menu;
SELECT clicks move the cursor one *selectable* row each (INFO rows, section headers
and blank rows are skipped); OCT+ enters or activates, hold OCT+ (700 ms) is a row's
second action, OCT- goes back; a page's cursor is reset to the top when it is entered
from its parent and kept when coming back from a sub-page or a confirm dialog; the
confirm dialog starts on "no" (one SELECT click up reaches "yes"); hidden rows
(Bluetooth host items with host mode off, a slot's load/clear while empty) are
skipped. The emulator turns an encoder one detent per 8 ms, so `eN:k` with k > 1 on
KNOB1..4 gets the firmware's fast-turn acceleration (x10 from the second click).

Main menu rows (selectable index): 0 Status, 1 Perform, 2 Bluetooth, 3 USB,
4 Routing, 5 MIDI filters, 6 Keyboard, 7 Button MIDI, 8 Presets, 9 MIDI log,
10 Composer, 11 Statistics, 12 Recorder, 13 Clock master, 14 MIDI reference,
15 Display, 16 System.

Buttons: 0 OCT-, 1 OCT+, 2 FX, 3 SCL, 4 ENV, 5 LFO, 6 EDIT, 7 GLO, 8 HOME, 9 SAVE,
10 ARP, 11 SEQ, 12 PLAY, 13 REC, 14+k the note keys (F3 = 0). Encoders: e0 SELECT,
e1 ALGO, e2..e5 KNOB1..4, e6 PRESETS.

Exit status 1 when any run halted or failed. Runs take about real time (0.9x) each.
"""
import argparse
import concurrent.futures
import os
import shutil
import subprocess
import sys

OCTDN, OCTUP, FX, SCL, ENV, LFO, EDIT, GLO, HOME, SAVE, ARP, SEQ, PLAY, REC = range(14)
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)


class Run:
    """One key script; every action advances the clock 300 ms."""

    def __init__(self, name):
        self.name, self.items, self.t, self.shots = name, [], 2400, []

    def press(self, key, ms=150):
        self.items += [f"{self.t}:{key}", f"{self.t + ms}:{key}^"]
        self.t += 300

    def hold(self, key, ms):
        self.items += [f"{self.t}:{key}", f"{self.t + ms}:{key}^"]
        self.t += ms + 300

    def enc(self, e, clicks):
        self.items.append(f"{self.t}:e{e}:{clicks}")
        self.t += 300

    def midi(self, hexs):
        self.items.append(f"{self.t}:m{hexs}")
        self.t += 300

    def wait(self, ms):
        self.t += ms

    def menu(self, index):
        """From the Status page: open the main menu, scroll to an item, enter it.
        The menu keeps its cursor between visits, so start from the top."""
        self.press(OCTUP)
        self.enc(0, -30)
        if index:
            self.enc(0, index)
        self.press(OCTUP)

    def shot(self, label, delay=200):
        """A picture of what is on the screen `delay` ms after the last action began
        (a press releases at 150 ms: use 250 or more to see what the release did)."""
        self.shots.append((label, self.t - 300 + delay))

    @property
    def end(self):
        return self.t + 500

    @property
    def keys(self):
        return ",".join(self.items)


def scripts():
    runs = []

    r = Run("splash_main")
    r.t = 900                                  # keys during the splash, then the status page and the menu itself
    r.press(OCTUP); r.enc(0, 3); r.t = 2400
    r.shot("home", 0)
    r.press(OCTUP); r.shot("menu"); r.enc(0, 16); r.shot("menu_end"); r.enc(0, -16); r.enc(2, 2); r.press(HOME)   # HOME from the menu: home
    r.press(HOME); r.press(OCTDN)                                      # HOME on the home page: the menu; OCT-: home again
    r.hold(OCTUP, 1500); r.hold(GLO, 1500); r.shot("toast", 50); r.hold(GLO, 1500); r.press(FX); r.midi("903c64"); r.midi("803c40")
    runs.append(r)

    r = Run("connections"); r.midi("903c64"); r.shot("page"); r.enc(0, 3); r.enc(2, 1); r.press(OCTUP); r.press(OCTDN); r.press(HOME); r.press(HOME); runs.append(r)

    r = Run("perform"); r.menu(1); r.shot("page"); r.press(OCTUP); r.press(OCTUP); r.midi("903c64"); r.shot("octave", 150); r.press(OCTDN)
    r.hold(14 + 5, 400); r.press(HOME); r.hold(HOME, 1200); r.shot("hold_home"); r.press(OCTDN); r.press(HOME); runs.append(r)

    r = Run("routing"); r.menu(4)
    r.press(OCTUP); r.enc(2, 1); r.enc(0, 3); r.shot("page"); r.hold(OCTUP, 900); r.shot("options")   # hold OCT+: the route's options
    r.enc(2, 1); r.enc(0, 1); r.enc(2, -1); r.press(OCTUP); r.enc(0, 2); r.press(OCTUP); r.press(OCTDN)
    r.shot("marks"); r.enc(0, 3); r.press(OCTUP); r.shot("inputs")                                    # cursor kept: 3 more rows reach Inputs
    r.enc(2, 2); r.enc(0, 1); r.press(OCTUP); r.enc(0, 1); r.enc(2, -1); r.press(OCTDN)
    r.enc(0, 17); r.midi("903c64"); r.press(OCTDN); runs.append(r)

    r = Run("midilog"); r.menu(9); r.midi("903c64"); r.midi("803c40"); r.midi("f8"); r.midi("b00740"); r.shot("page")
    for i in range(16): r.midi("90%02x64" % (0x30 + i))
    r.enc(0, -1); r.shot("scrolled"); r.hold(OCTUP, 900); r.midi("f8"); r.shot("paused"); r.hold(OCTUP, 900)   # 4 lines back, then held
    r.enc(0, 2); r.press(OCTUP); r.midi("903c64"); r.shot("decoded"); r.enc(2, 1); r.press(OCTUP); r.press(OCTDN); runs.append(r)

    r = Run("composer"); r.menu(10)
    r.press(OCTUP); r.enc(2, 1); r.enc(0, 1); r.enc(2, 2); r.shot("page"); r.enc(0, 1); r.enc(2, 5); r.enc(0, 1); r.enc(2, -3)
    r.enc(0, 1); r.press(OCTUP); r.enc(0, 1); r.press(OCTUP); r.shot("sent", 250)
    r.enc(0, 1); r.hold(OCTUP, 900); r.shot("stored", 50); r.press(OCTUP)                           # Fav 1: hold stores, OCT+ sends
    r.enc(0, 3); r.press(OCTUP); r.enc(0, -1); r.press(OCTDN); runs.append(r)

    r = Run("stats"); r.menu(11); r.midi("903c64"); r.midi("803c40"); r.shot("page"); r.enc(0, 3); r.enc(2, 1); r.press(OCTUP); r.enc(0, -2); r.press(OCTDN); runs.append(r)

    r = Run("recorder"); r.menu(12)
    r.press(OCTUP); r.shot("page"); r.midi("903c64"); r.midi("803c40"); r.press(OCTUP)              # Record is the first selectable row
    r.enc(0, 1); r.press(OCTUP); r.wait(500); r.press(OCTUP); r.enc(0, 1); r.press(OCTUP); r.enc(0, 1); r.enc(2, 1); r.press(OCTUP)
    r.enc(0, 1); r.press(OCTUP); r.press(REC); r.midi("903c64"); r.press(REC); r.press(PLAY); r.wait(500); r.press(PLAY)
    r.enc(0, 4); r.press(OCTDN); runs.append(r)

    r = Run("reference"); r.menu(14); r.enc(0, 5); r.shot("page"); r.enc(2, 2); r.press(OCTUP); r.enc(0, -3); r.enc(6, 4); r.enc(6, -2); r.enc(0, 40); r.press(OCTDN); runs.append(r)

    r = Run("clock"); r.menu(13)
    r.press(OCTUP); r.wait(600); r.shot("page"); r.enc(0, 1); r.enc(2, 5); r.enc(2, -20); r.enc(0, 1); r.press(OCTUP); r.press(OCTUP)
    r.press(PLAY); r.wait(400); r.press(PLAY); r.enc(0, -2); r.press(OCTUP); r.enc(0, 6); r.press(OCTDN); runs.append(r)

    r = Run("keyboard"); r.menu(6)                                       # the cursor starts on Octave
    r.press(OCTUP); r.press(OCTUP); r.enc(0, 1); r.shot("page"); r.press(OCTUP); r.press(OCTDN)      # velocity +10, -10
    r.enc(0, -2); r.press(OCTUP); r.hold(OCTUP, 900); r.hold(OCTDN, 900); r.shot("channel")          # channel 2; held OCT: octave
    r.enc(0, 3); r.press(OCTUP); r.press(OCTUP); r.enc(0, 1); r.enc(6, 2); r.enc(0, 1); r.enc(1, 1)
    r.enc(2, 3); r.enc(3, 2); r.enc(4, -1); r.enc(5, 1); r.hold(14 + 5, 400); r.hold(14 + 9, 400); r.hold(14 + 26, 300)
    r.enc(0, 8); r.press(SAVE); runs.append(r)                       # OCT- adjusts here: SAVE leaves

    r = Run("buttons"); r.menu(7)
    r.press(FX); r.press(OCTUP); r.shot("page"); r.enc(2, 3); r.press(SCL); r.press(OCTUP); r.press(OCTUP); r.enc(2, -1)
    r.enc(0, 9); r.enc(6, 2); r.press(OCTUP); r.enc(2, 1); r.press(14 + 3); r.press(OCTUP); r.enc(0, 5); r.press(OCTDN)
    r.press(FX); r.press(SCL); runs.append(r)                        # mapped buttons send MIDI from the main menu

    r = Run("bluetooth"); r.menu(2)                                      # Pairable is the first selectable row
    r.press(OCTUP); r.shot("page"); r.press(OCTUP); r.enc(0, 1); r.press(OCTUP); r.enc(2, 1); r.enc(2, -1)
    r.enc(0, 1); r.press(OCTUP); r.enc(0, 1); r.press(OCTUP); r.shot("confirm"); r.press(OCTDN)   # forget bonds: confirm, no
    r.enc(0, 1); r.press(OCTUP)                                                                   # host mode on (cursor was kept)
    r.enc(0, 1); r.press(OCTUP); r.wait(1500); r.shot("scan", 0); r.enc(0, 1); r.press(OCTUP); r.wait(500); r.press(OCTDN)
    r.enc(0, 1); r.press(OCTUP); r.enc(0, 1); r.press(OCTUP)                                      # pair on connect, auto reconnect
    r.enc(0, -3); r.press(OCTUP); r.press(OCTDN); runs.append(r)                                  # host mode off, back

    r = Run("usb"); r.menu(3); r.shot("page"); r.press(OCTUP); r.enc(2, 1); r.press(OCTUP); r.press(OCTUP); r.enc(0, 1); r.enc(0, -3); r.press(OCTDN); runs.append(r)

    r = Run("filters"); r.menu(5)
    r.press(OCTUP); r.enc(0, 1); r.shot("page"); r.press(OCTUP); r.enc(0, 1); r.enc(2, 1); r.midi("f8"); r.midi("fe"); r.midi("f07e7f0601f7")
    r.enc(0, 1); r.press(OCTUP); r.shot("panic", 250); r.enc(0, 1); r.press(OCTUP); r.enc(0, 1); r.press(OCTUP); r.enc(0, -8); r.press(OCTDN); runs.append(r)

    r = Run("display"); r.menu(15); r.press(OCTUP); r.shot("page"); r.enc(2, 2); r.enc(2, -3); r.enc(0, 1); r.press(OCTUP); r.wait(500); r.press(OCTUP); r.enc(0, -1); r.press(OCTDN); runs.append(r)

    r = Run("backlight"); r.menu(15); r.press(OCTUP); r.press(OCTUP); r.press(OCTUP); r.press(OCTUP)   # 30 s -> 1 min -> 5 min -> never -> 10 s
    r.press(HOME); r.wait(11000); r.shot("dark", 0); r.hold(14 + 7, 300); r.shot("woke", 50)        # a key with the panel dark: note sent, panel on
    runs.append(r)

    r = Run("system"); r.menu(16)
    r.press(OCTUP); r.shot("toast", 250)                              # reset crash counters (first selectable row)
    r.enc(0, 1); r.press(OCTUP); r.shot("confirm"); r.press(OCTDN)    # reboot: confirm, no (cursor kept on Reboot)
    r.enc(0, 1); r.press(OCTUP); r.press(OCTUP)                       # usb recovery: confirm, OCT+ on "no" = no
    r.enc(0, 2); r.press(OCTUP)                                       # Input test
    r.press(FX); r.press(14 + 2); r.enc(0, 2); r.enc(1, -1); r.enc(2, 1); r.enc(3, 1); r.enc(4, 1); r.enc(5, 1); r.enc(6, 1)
    r.shot("inputtest"); r.hold(SEQ, 1200); r.press(HOME)             # HOME leaves to the home page
    r.menu(8)                                                         # Presets
    r.press(OCTUP); r.shot("presets"); r.enc(0, -1); r.press(OCTUP); r.shot("saved", 250)   # empty slot: save, confirm yes
    r.press(OCTUP); r.enc(0, -1); r.press(OCTUP)                      # used slot: load, confirm yes
    r.hold(OCTUP, 900); r.shot("slot"); r.enc(0, 2); r.press(OCTUP); r.press(OCTDN); r.press(OCTDN)   # slot page: clear, confirm no, back
    r.enc(0, 20); r.enc(0, -30); r.press(OCTDN)
    r.enc(0, 8); r.press(OCTUP); r.enc(0, 3); r.press(OCTUP); r.enc(0, -1); r.press(OCTUP)   # System > reset settings: confirm, yes
    r.wait(1500); r.enc(0, 3); r.press(OCTDN); runs.append(r)
    return runs


def one(emu, fwsc, out, name, ms, keys):
    d = os.path.join(out, name)
    os.makedirs(os.path.join(d, "work"), exist_ok=True)
    with open(os.path.join(d, "keys.txt"), "w") as f:
        f.write(keys + "\n")
    env = dict(os.environ, FM1_BENCH_KEYS=keys)
    p = subprocess.run([os.path.join(emu, "fm1-live"), "--fwsc", fwsc, "--shot", str(ms)],
                       cwd=d, env=env, capture_output=True, text=True)
    text = p.stdout + p.stderr
    with open(os.path.join(d, "out.txt"), "w") as f:
        f.write(text)
    halts = [l for l in text.splitlines() if "halt" in l]
    ppm = os.path.join(d, "work", "panel.ppm")
    if os.path.exists(ppm) and shutil.which("convert"):
        subprocess.run(["convert", ppm, "-crop", "520x520+350+25", os.path.join(d, "panel.png")], capture_output=True)
    return name, p.returncode, halts


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--emu", default=os.path.join(ROOT, "..", "..", "fm1-emu", "target", "release"), help="fm1-emu's target/release")
    ap.add_argument("--fwsc", default=os.path.join(ROOT, "build", "fm1bridge.fwsc"))
    ap.add_argument("--out", default=os.path.join(ROOT, "build", "ui_walk"))
    ap.add_argument("--jobs", type=int, default=max(1, (os.cpu_count() or 2) - 1))
    ap.add_argument("--only", help="one run by name")
    ap.add_argument("--list", action="store_true", help="print the scripts and exit")
    a = ap.parse_args()
    runs = [r for r in scripts() if not a.only or r.name == a.only]
    if a.list:
        for r in runs:
            print(f"{r.name}\t{r.end}\t{r.keys}")
            for label, ms in r.shots:
                print(f"  {label} at {ms} ms")
        return 0
    if not os.path.exists(os.path.join(a.emu, "fm1-live")):
        sys.exit(f"ui_walk: {a.emu}/fm1-live missing (cargo build --release in fm1-emu)")
    if not os.path.exists(a.fwsc):
        sys.exit(f"ui_walk: {a.fwsc} missing (make -C app && tools/package.sh)")
    jobs = [(r.name, r.end, r.keys) for r in runs]
    jobs += [(f"{r.name}_{label}", ms, r.keys) for r in runs for label, ms in r.shots]
    bad = 0
    with concurrent.futures.ThreadPoolExecutor(a.jobs) as ex:
        futs = [ex.submit(one, a.emu, os.path.abspath(a.fwsc), os.path.abspath(a.out), *j) for j in jobs]
        for f in concurrent.futures.as_completed(futs):
            name, code, halts = f.result()
            status = "ok" if code == 0 and not halts else "HALT" if halts else f"exit {code}"
            bad += status != "ok"
            print(f"{name:22s} {status}" + (f"  {halts[0]}" if halts else ""))
    pics = [os.path.join(n, "panel.png") for n, _, _ in jobs]
    pics = [p for p in pics if os.path.exists(os.path.join(a.out, p))]
    if pics and shutil.which("montage"):              # labelled with the run's directory name
        subprocess.run(["montage", "-label", "%d", *pics, "-tile", "4x", "-geometry", "300x300+4+4",
                        "-pointsize", "14", "walk.png"], cwd=a.out, capture_output=True)
        print(f"pictures: {os.path.join(a.out, 'walk.png')}")
    print(f"{len(jobs)} runs, {bad} not ok")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
