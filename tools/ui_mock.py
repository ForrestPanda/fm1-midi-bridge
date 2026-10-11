#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Render example screenshots of every UI page into docs/ui-examples/ with the real
panel font (app/ui/font.h) and the layout rules of app/ui/ui_menu.c (30 x 17 cells,
title and cursor rows inverted, values right-aligned, INFO rows dim, footer hints).
The contents are examples, not captures from hardware.

  ui_mock.py [OUT_DIR] [--scale 2]
"""
import re, sys, os
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
out_dir = sys.argv[1] if len(sys.argv) > 1 and not sys.argv[1].startswith('--') else os.path.join(ROOT, 'docs', 'ui-examples')
scale = 2
if '--scale' in sys.argv: scale = int(sys.argv[sys.argv.index('--scale') + 1])

src = open(os.path.join(ROOT, 'app', 'ui', 'font.h')).read()
glyphs = [[int(x, 16) for x in re.findall(r'0x[0-9a-f]{2}', row)] for row in re.findall(r'\{([^}]*)\}', src.split('font_glyphs')[1])]
W, H, FW, FH = 240, 240, 8, 14
COLS, ROWS = W // FW, H // FH
BG, TXT, DIM, OK, WARN = (0, 0, 0), (255, 255, 255), (132, 130, 132), (0, 255, 0), (255, 165, 0)

class Screen:
    def __init__(self):
        self.img = Image.new('RGB', (W, H), BG); self.px = self.img.load()
    def row(self, r, text, fg=TXT, bg=BG):
        text = (text + ' ' * COLS)[:COLS]
        for i, ch in enumerate(text):
            g = glyphs[ord(ch) - 0x20 if 0x20 <= ord(ch) <= 0x7e else 0]
            for y in range(FH):
                for x in range(FW):
                    self.px[i * FW + x, r * FH + y] = fg if g[y] & (0x80 >> x) else bg
    def inv(self, r, text): self.row(r, text, BG, TXT)
    def save(self, name):
        im = self.img.resize((W * scale, H * scale), Image.NEAREST)
        im.save(os.path.join(out_dir, name)); return im

def menu(s, title, items, cursor=0, footer="SEL:move OCT+:ok OCT-:back"):
    """items: (label, value, kind) kind in action/toggle/enum/info/submenu"""
    s.inv(0, ' ' + title)
    r = 1
    for i, (label, value, kind) in enumerate(items[:ROWS - 2]):
        line = ' ' + label
        val = value if value is not None else ('>' if kind == 'submenu' else '')
        if val:
            pos = max(COLS - 1 - len(val), len(line) + 1)
            line = line.ljust(pos) + val
        if i == cursor: s.inv(r, line)
        else: s.row(r, line, DIM if kind == 'info' else TXT)
        r += 1
    s.row(ROWS - 1, footer, DIM)

os.makedirs(out_dir, exist_ok=True)
shots = []
# 01 splash
s = Screen(); s.row(5, "        FM-1 MIDI bridge"); s.row(7, "            v0.1.0", DIM)
s.row(10, "   TRS / USB / BLE  <->  MIDI", DIM); s.row(13, "     USB:ok   BLE:--", OK); shots.append(s.save('01-splash.png'))
# 02 main
s = Screen(); menu(s, "FM-1 MIDI bridge", [(x, None, 'submenu') for x in ["Connections", "Routing", "MIDI log", "Composer", "Statistics", "Recorder", "MIDI reference", "Clock master", "Keyboard", "Button MIDI", "Bluetooth", "USB", "MIDI filters", "Display", "System"]], 0, "USB:ok BLE:adv ECO BAT:3.87V")
shots.append(s.save('02-main-menu.png'))
# 03 routing
s = Screen(); menu(s, "Routing", [("TRS in  -> BLE out", "on", 'toggle'), ("TRS in  -> USB out", "on", 'toggle'), ("USB in  -> BLE out", "on", 'toggle'), ("BLE in  -> USB out", "off", 'toggle'), ("Keys    -> USB out", "on", 'toggle'), ("Keys    -> BLE out", "on", 'toggle'), ("", "", 'info'), ("Options TRS->BLE", None, 'submenu'), ("Options TRS->USB", None, 'submenu'), ("Options USB->BLE", None, 'submenu'), ("Options BLE->USB", None, 'submenu'), ("Options Keys->USB", None, 'submenu'), ("Options Keys->BLE", None, 'submenu'), ("Inputs", None, 'submenu'), ("", "", 'info'), ("TRS in/out", "1532/0", 'info'), ("USB in/out", "12/1544", 'info'), ("BLE in/out", "0/1532", 'info')], 3)
shots.append(s.save('03-routing.png'))
# 04 log
s = Screen(); s.row(0, "USB:ok BLE:ok  TB TU UB BU", OK)
lines = ["TRS 90 3C 64", "TRS 80 3C 00", "TRS 90 40 64", "USB B0 07 7F", "BLE 90 48 50", "BLE F8", "TRS B0 01 22", "TRS E0 00 40", "USB F0 7E 7F 09 01 F7", "TRS 90 43 70", "TRS C0 05", "BLE 80 48 00", "TRS F8", "TRS 90 3E 60", "USB FA", "TRS 80 3E 00"]
for i, l in enumerate(lines): s.row(1 + i, l, TXT if i == len(lines) - 1 else DIM)
shots.append(s.save('04-midi-log.png'))
# 05 bluetooth
s = Screen(); menu(s, "Bluetooth", [("State", "advertising", 'info'), ("Name", "FM-1 MIDI", 'info'), ("Pairable", "on", 'toggle'), ("Link", "low latency", 'enum'), ("Disconnect", "", 'action'), ("Forget bonds", "2", 'action'), ("", "", 'info'), ("Host mode", "off", 'toggle')], 3)
shots.append(s.save('05-bluetooth.png'))
# 06 bluetooth host mode with scan
s = Screen(); menu(s, "Bluetooth", [("State", "host: scanning", 'info'), ("Name", "FM-1 MIDI", 'info'), ("Pairable", "off", 'toggle'), ("Link", "power saving", 'enum'), ("Disconnect", "", 'action'), ("Forget bonds", "2", 'action'), ("", "", 'info'), ("Host mode", "on", 'toggle'), ("Scan devices", None, 'submenu'), ("Pair on connect", "off", 'toggle'), ("Auto reconnect", "on", 'toggle')], 8)
shots.append(s.save('06-bluetooth-host.png'))
# 07 scan
s = Screen(); s.inv(0, " Scan devices"); s.row(1, " scanning...          3 found", DIM)
s.inv(2, " microKEY Air          -58dBm"); s.row(3, " Xkey Air 25           -71dBm"); s.row(4, " 4C:11:AE:02:9F:31     -83dBm")
s.row(ROWS - 1, "SEL OCT+:connect OCT-:back", DIM); shots.append(s.save('07-scan.png'))
# 08 usb
s = Screen(); menu(s, "USB", [("Cable", "yes", 'info'), ("Host", "enumerated", 'info'), ("USB MIDI", "on", 'toggle'), ("USB in/out", "12/1544", 'info')], 2)
shots.append(s.save('08-usb.png'))
# 09 filters
s = Screen(); menu(s, "MIDI filters", [("Drop clock (F8)", "on", 'toggle'), ("Drop active sense", "off", 'toggle'), ("Drop SysEx", "off", 'toggle'), ("Dropped", "4096", 'info'), ("", "", 'info'), ("Panic (all off)", "sending...", 'action'), ("Panic on link loss", "on", 'toggle'), ("Note collision guard", "on", 'toggle')], 5)
shots.append(s.save('09-filters.png'))
# 10 display
s = Screen(); menu(s, "Display", [("Backlight off after", "30 s", 'enum'), ("Invert colours", "off", 'toggle')], 0)
shots.append(s.save('10-display.png'))
# 11 system
s = Screen(); menu(s, "System", [("Firmware", "FM-1_9B1", 'info'), ("Version", "v0.1.0", 'info'), ("Built", "Oct 11 2026", 'info'), ("Battery", "3870 mV (pin)", 'info'), ("Uptime", "0:12:41", 'info'), ("TRS in/out", "1532/0", 'info'), ("USB in/out", "12/1544", 'info'), ("BLE in/out", "0/1532", 'info'), ("Input drops", "0", 'info'), ("Boots", "17", 'info'), ("Last reset", "power on", 'info'), ("Crashes", "wdt 0 hang 0", 'info'), ("Reset crash counters", "", 'action'), ("", "", 'info'), ("Reboot", "", 'action'), ("USB recovery mode", "", 'action'), ("Reset settings", "", 'action'), ("Presets", None, 'submenu'), ("Input test", None, 'submenu')], 10)
shots.append(s.save('11-system.png'))
# 12 confirm
s = Screen(); s.inv(0, " Confirm"); s.row(3, "Reset all settings?"); s.row(6, "  OCT+  yes", OK); s.row(7, "  OCT-  no", DIM)
shots.append(s.save('12-confirm.png'))
# 14 keyboard
s = Screen(); menu(s, "Keyboard", [("Keys send MIDI", "on", 'toggle'), ("Channel", "1", 'enum'), ("Octave  (OCT-/+)", "+1  (F4..G6)", 'enum'), ("Velocity", "100", 'enum'), ("Velocity source", "fixed", 'enum'), ("Program (PRESETS)", "12", 'enum'), ("Bank MSB (ALGO)", "0", 'enum'), ("", "", 'info'), ("KNOB1", "CC1 = 0", 'info'), ("KNOB2", "CC74 = 64", 'info'), ("KNOB3", "CC71 = 0", 'info'), ("KNOB4", "CC7 = 100", 'info'), ("", "", 'info'), ("Knobs send CCs on this page", "", 'info')], 2, "SEL:move OCT:octave SAVE:back")
shots.append(s.save('14-keyboard.png'))
# 15 route options
s = Screen(); menu(s, "TRS -> BLE", [("Channels", "all", 'enum'), ("Remap channel", "to 10", 'enum'), ("Clock / transport", "off", 'toggle'), ("SysEx", "off", 'toggle')], 1)
shots.append(s.save('15-route-options.png'))
# 16 inputs
s = Screen(); menu(s, "Inputs", [("TRS transpose", "+12", 'enum'), ("TRS velocity curve", "soft", 'enum'), ("TRS fixed velocity", "100", 'enum')], 1)
shots.append(s.save('16-inputs.png'))
# 17 button MIDI
s = Screen(); menu(s, "Button MIDI", [("FX", "none", 'enum'), ("SCL", "none", 'enum'), ("ENV", "note 36", 'enum'), ("LFO", "none", 'enum'), ("EDIT", "none", 'enum'), ("SEQ", "program 5", 'enum'), ("ARP", "none", 'enum'), ("PLAY", "CC 64", 'enum'), ("REC", "CC 65", 'enum'), ("", "", 'info'), ("Key (PRESETS picks)", "C4: CC 66", 'enum'), ("", "", 'info'), ("OCT+ cycles the kind, KNOB1", "", 'info'), ("sets the number. Channel:", "", 'info'), ("the keyboard's. Or press", "", 'info'), ("a key to pick it.", "", 'info')], 10)
shots.append(s.save('17-button-midi.png'))
# 18 presets
s = Screen(); menu(s, "Presets", [("Preset 1", "used", 'info'), ("  load", "", 'action'), ("  save here", "", 'action'), ("  clear", "", 'action'), ("Preset 2", "empty", 'info'), ("  save here", "", 'action'), ("Preset 3", "empty", 'info'), ("  save here", "", 'action'), ("Preset 4", "empty", 'info'), ("  save here", "", 'action')], 1)
shots.append(s.save('18-presets.png'))
# 19 composer
s = Screen(); menu(s, "Composer", [("Kind", "CC", 'enum'), ("Channel", "1", 'enum'), ("Data 1", "64 sustain", 'enum'), ("Data 2", "127", 'enum'), ("To", "USB+BLE", 'enum'), ("Send", "1 CC64 sustain 127", 'action'), ("", "", 'info'), ("Fav 1 send", "1 NoteOn C4 100", 'action'), ("  store here", "", 'action'), ("Fav 2 send", "SysEx inquiry", 'action'), ("  store here", "", 'action'), ("Fav 3 send", "empty", 'action'), ("  store here", "", 'action'), ("Fav 4 send", "empty", 'action'), ("  store here", "", 'action')], 5, "KNOB1:value OCT+:send/cycle")
shots.append(s.save('19-composer.png'))
# 20 connections
s = Screen(); s.inv(0, " Connections")
s.row(1, " TRS  jack  in 1532       *"); s.row(2, " USB  ok    in 12     out 1544 ", OK); s.row(3, " BLE  ok    in 0      out 1532*", OK); s.row(4, " KBD  on    in 40")
s.row(6, " Routes            on   opts", DIM)
for i, l in enumerate([("  TRS -> USB        on   ", TXT), ("  TRS -> BLE        on   noclk", TXT), ("  USB -> BLE        on   ", TXT), ("  BLE -> USB        off  ", DIM), ("  KBD -> USB        on   ch map ", TXT), ("  KBD -> BLE        on   ", TXT)]):
    s.row(7 + i, l[0], l[1])
s.row(13, " filters clk   dropped 4096", DIM); s.row(ROWS - 1, "* = activity   OCT-:back", DIM); shots.append(s.save('20-connections.png'))
# 21 decoded log
s = Screen(); s.row(0, "USB:ok BLE:ok  TB TU UB BU", OK)
lines = ["TRS 1 NoteOn C4 100", "TRS 1 NoteOff C4 0", "TRS 1 NoteOn E4 100", "USB 1 CC7 volume 127", "BLE 1 NoteOn C5 80", "BLE Clock", "TRS 1 CC1 mod 34", "TRS 1 Bend +0", "USB SysEx inquiry", "TRS 1 NoteOn G4 112", "TRS 1 Program 6", "BLE 1 NoteOff C5 0", "TRS Clock", ">USB 1 CC64 sustain 127", "USB Start", "TRS 1 NoteOff E4 0"]
for i, l in enumerate(lines): s.row(1 + i, l, TXT if i == len(lines) - 1 else DIM)
shots.append(s.save('21-midi-log-decoded.png'))
# 22 statistics
s = Screen(); s.inv(0, " Statistics       OCT+:reset")
for i, (k, v) in enumerate([("notes", 1532), ("CC", 212), ("program", 3), ("bend", 88), ("aftertouch", 0), ("clock", 48211), ("transport", 4), ("SysEx", 2), ("other", 0)]):
    s.row(1 + i, " %-11s %10d" % (k, v), TXT if v else DIM)
s.row(10, " notes C2..G6  vel 31..127"); s.row(11, " clock tempo 120.0 bpm"); s.row(12, " ch 1.3......0.....", DIM); s.row(13, " (1-9, 0 = 10, a-f = 11-16)", DIM); s.row(ROWS - 1, "OCT-:back", DIM)
shots.append(s.save('22-statistics.png'))
# 23 recorder
s = Screen(); menu(s, "Recorder", [("State", "PLAY 41/120", 'info'), ("Record", "start", 'action'), ("Play", "stop", 'action'), ("Loop", "on", 'toggle'), ("Play to", "USB+BLE", 'enum'), ("Clear", "", 'action'), ("", "", 'info'), ("Records what the inputs send", "", 'info'), ("(TRS, USB, BLE), up to 2048", "", 'info'), ("events. REC toggles recording.", "", 'info')], 2)
shots.append(s.save('23-recorder.png'))
# 24 reference
s = Screen(); s.inv(0, " MIDI reference    SEL:scroll")
ref = ["STATUS BYTES (n = channel 0-F)", " 8n note off   note  vel", " 9n note on    note  vel", "    (vel 0 = note off)", " An poly AT    note  pres", " Bn control    cc#   value", " Cn program    prog", " Dn chan AT    pres", " En pitch bend lsb   msb", "    (00 40 = centre)", " F0 .. F7      SysEx", " F1 MTC  F2 song pos  F3 sel", " F6 tune request", " F8 clock (24 per quarter)", " FA start FB continue FC stop"]
for i, l in enumerate(ref): s.row(1 + i, l, OK if l and l[0] != ' ' else TXT)
s.row(ROWS - 1, "OCT-:back", DIM); shots.append(s.save('24-reference.png'))
# 25 clock master
s = Screen(); menu(s, "Clock master", [("Clock", "running", 'action'), ("Tempo (bpm)", "120", 'enum'), ("Send to", "USB+BLE", 'enum'), ("", "", 'info'), ("Sends F8 at 24 per quarter,", "", 'info'), ("Start on run, Stop on stop.", "", 'info'), ("PLAY toggles it on this page.", "", 'info')], 1)
shots.append(s.save('25-clock-master.png'))
# 13 input test
s = Screen(); s.inv(0, " Input test        HOME:leave"); s.row(1, " cells: 7,1 0,0"); s.row(2, " held: HOME", OK)
for i, (n, v, r) in enumerate([("SELECT", 12, "00"), ("ALGO", 0, "00"), ("KNOB1", -3, "11"), ("KNOB2", 0, "00"), ("KNOB3", 0, "00"), ("KNOB4", 0, "00"), ("PRESETS", 1, "00")]):
    s.row(4 + i, " %-8s %+5d   rest %s" % (n, v, r))
s.row(12, " last: SELECT +1", DIM); s.row(13, " frames 41203  drops 0", DIM); shots.append(s.save('13-input-test.png'))
# contact sheet
cols = 4; rows = (len(shots) + cols - 1) // cols; pad = 10; sw = W * scale
sheet = Image.new('RGB', (cols * (sw + pad) + pad, rows * (sw + pad) + pad), (40, 40, 40))
for i, im in enumerate(shots):
    sheet.paste(im, (pad + (i % cols) * (sw + pad), pad + (i // cols) * (sw + pad)))
sheet.save(os.path.join(out_dir, 'all-pages.png'))
print("wrote", len(shots), "screens to", out_dir)
