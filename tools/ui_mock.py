#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Render example screenshots of every UI page into docs/ui-examples/ with the real
panel fonts (app/ui/font.h, app/ui/font_big.h) and the layout rules of app/ui/ui_menu.c
(30 x 17 cells, title row on a dark blue band with the link status on the right,
cursor row inverted, values right-aligned, INFO rows dim, a toggle's value green when on and dim
when off, footer hints). The contents are examples, not captures from hardware; real
captures from the emulator come out of tools/ui_walk.py.

  ui_mock.py [OUT_DIR] [--scale 2]
"""
import re, sys, os
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
out_dir = sys.argv[1] if len(sys.argv) > 1 and not sys.argv[1].startswith('--') else os.path.join(ROOT, 'docs', 'ui-examples')
scale = 2
if '--scale' in sys.argv: scale = int(sys.argv[sys.argv.index('--scale') + 1])


def load_font(name, table):
    src = open(os.path.join(ROOT, 'app', 'ui', name)).read()
    return [[int(x, 16) for x in re.findall(r'0x[0-9a-f]+', row)] for row in re.findall(r'\{([^}]*)\}', src.split(table)[1])]


glyphs = load_font('font.h', 'font_glyphs')
big = load_font('font_big.h', 'font_big_glyphs')
W, H, FW, FH = 240, 240, 8, 14
BW, BH = 16, 28
COLS, ROWS = W // FW, H // FH
BG, TXT, DIM, OK, WARN = (0, 0, 0), (255, 255, 255), (132, 130, 132), (0, 255, 0), (255, 165, 0)
TITLE_BG, TITLE_ST = (0, 72, 112), (160, 200, 224)      # the title band and its status text (ui.c)
STATUS = "U:ok B:ok"            # the title row's right side on every page


class Screen:
    def __init__(self):
        self.img = Image.new('RGB', (W, H), BG); self.px = self.img.load()

    def text(self, col, r, text, fg=TXT, bg=BG):
        for i, ch in enumerate(text):
            g = glyphs[ord(ch) - 0x20 if 0x20 <= ord(ch) <= 0x7e else 0]
            for y in range(FH):
                for x in range(FW):
                    self.px[(col + i) * FW + x, r * FH + y] = fg if g[y] & (0x80 >> x) else bg

    def row(self, r, text, fg=TXT, bg=BG, split=None, fg2=None):
        text = (text + ' ' * COLS)[:COLS]
        if split is None:
            self.text(0, r, text, fg, bg)
        else:
            self.text(0, r, text[:split], fg, bg); self.text(split, r, text[split:], fg2, bg)

    def inv(self, r, text): self.row(r, text, BG, TXT)

    def title(self, title, status=STATUS):
        line = ' ' + title
        pos = max(COLS - 1 - len(status), len(line) + 1)
        self.row(0, line.ljust(pos) + status, TXT, TITLE_BG, pos, TITLE_ST)

    def big(self, col, r, text, fg=TXT):
        for i, ch in enumerate(text):
            g = big[ord(ch) - 0x20 if 0x20 <= ord(ch) <= 0x7e else 0]
            for y in range(BH):
                for x in range(BW):
                    self.px[(col + i) * BW + x, r * BH + y] = fg if g[y] & (0x8000 >> x) else BG

    def save(self, name):
        im = self.img.resize((W * scale, H * scale), Image.NEAREST)
        im.save(os.path.join(out_dir, name)); return im


def menu(s, title, items, cursor=0, footer="SEL:move OCT+:ok OCT-:back", toast=None, status=STATUS):
    """items: (label, value, kind) kind in action/toggle/enum/info/submenu; a 'warn' kind
    is an info row in the warn colour"""
    s.title(title, status)
    r = 1
    for i, (label, value, kind) in enumerate(items[:ROWS - 2]):
        line = ' ' + label
        val = value if value is not None else ('>' if kind == 'submenu' else '')
        pos = None
        if val:
            pos = max(COLS - 1 - len(val), len(line) + 1)
            line = line.ljust(pos) + val
        if i == cursor: s.inv(r, line)
        elif kind == 'warn': s.row(r, line, WARN)
        elif kind == 'toggle' and val: s.row(r, line, TXT, BG, pos, DIM if val.startswith('off') else OK)
        else: s.row(r, line, DIM if kind == 'info' else TXT)
        r += 1
    if toast: s.row(ROWS - 1, ' ' + toast, OK)
    else: s.row(ROWS - 1, footer, DIM)


def log_page(s, lines, status="USB:ok BLE:ok  TB TU UB ", held=None):
    """held: None (following), 'PAUSED' or '^N'"""
    if held: s.row(0, status[:COLS - len(held) - 1].ljust(COLS - len(held)) + held, TXT, TITLE_BG)
    else: s.row(0, status, OK, TITLE_BG)
    for i, l in enumerate(lines): s.row(1 + i, l, TXT if i == len(lines) - 1 else DIM)


os.makedirs(out_dir, exist_ok=True)
shots = []
# 01 splash
s = Screen(); s.row(5, "        FM-1 MIDI bridge"); s.row(7, "            v0.2.0", DIM)
s.row(10, "   TRS / USB / BLE  <->  MIDI", DIM); s.row(13, "     USB:ok   BLE:--", OK); shots.append(s.save('01-splash.png'))
# 02 main menu: sections, battery footer
main = [("Status", None, 'submenu'), ("Perform", None, 'submenu'), ("", "", 'info'), ("SETUP", "", 'info'), ("Bluetooth", None, 'submenu'), ("USB", None, 'submenu'), ("Routing", None, 'submenu'), ("MIDI filters", None, 'submenu'), ("Keyboard", None, 'submenu'), ("Button MIDI", None, 'submenu'), ("Presets", None, 'submenu'), ("", "", 'info'), ("BENCH", "", 'info'), ("MIDI log", None, 'submenu'), ("Composer", None, 'submenu'), ("Statistics", None, 'submenu'), ("Recorder", None, 'submenu'), ("Clock master", None, 'submenu'), ("MIDI reference", None, 'submenu'), ("", "", 'info'), ("DEVICE", "", 'info'), ("Display", None, 'submenu'), ("System", None, 'submenu')]
s = Screen(); menu(s, "FM-1 MIDI bridge", main, 4, " bat [###.]  ECO")
shots.append(s.save('02-main-menu.png'))
# 03 routing: one row per route, hold OCT+ for its options
s = Screen(); menu(s, "Routing", [("TRS -> BLE", "on noclk", 'toggle'), ("TRS -> USB", "on", 'toggle'), ("USB -> BLE", "on", 'toggle'), ("BLE -> USB", "off", 'toggle'), ("Keys -> USB", "on ch map", 'toggle'), ("Keys -> BLE", "on", 'toggle'), ("", "", 'info'), ("Inputs", None, 'submenu'), ("", "", 'info'), ("ch = channel filter, map =", "", 'info'), ("remap, noclk / nosx = blocked", "", 'info'), ("", "", 'info'), ("TRS in/out", "1532/0", 'info'), ("USB in/out", "12/1544", 'info'), ("BLE in/out", "0/1532", 'info')], 3, "OCT+:toggle hold:options")
shots.append(s.save('03-routing.png'))
# 04 log
raw = ["TRS 90 3C 64", "TRS 80 3C 00", "TRS 90 40 64", "USB B0 07 7F", "BLE 90 48 50", "BLE F8", "TRS B0 01 22", "TRS E0 00 40", "USB F0 7E 7F 06 01 F7", "TRS 90 43 70", "TRS C0 05", "BLE 80 48 00", "TRS F8", "TRS 90 3E 60", "USB FA", "TRS 80 3E 00"]
s = Screen(); log_page(s, raw); shots.append(s.save('04-midi-log.png'))
# 05 bluetooth
s = Screen(); menu(s, "Bluetooth", [("State", "advertising", 'info'), ("Name", "FM-1 MIDI", 'info'), ("Pairable", "on", 'toggle'), ("Link", "low latency", 'enum'), ("Disconnect", "", 'action'), ("Forget bonds", "2", 'action'), ("", "", 'info'), ("Host mode", "off", 'toggle')], 3, status="U:ok B:adv")
shots.append(s.save('05-bluetooth.png'))
# 06 bluetooth host mode with scan
s = Screen(); menu(s, "Bluetooth", [("State", "host: scanning", 'info'), ("Name", "FM-1 MIDI", 'info'), ("Pairable", "off", 'toggle'), ("Link", "power saving", 'enum'), ("Disconnect", "", 'action'), ("Forget bonds", "2", 'action'), ("", "", 'info'), ("Host mode", "on", 'toggle'), ("Scan devices", None, 'submenu'), ("Pair on connect", "off", 'toggle'), ("Auto reconnect", "on", 'toggle')], 8)
shots.append(s.save('06-bluetooth-host.png'))
# 07 scan
s = Screen(); s.title("Scan devices", "U:ok B:host"); s.row(1, " %-21s %d found" % ("scanning", 3), DIM)
for i, (who, rssi) in enumerate([("microKEY Air", -58), ("Xkey Air 25", -71), ("4C:11:AE:02:9F:31", -83)]):
    (s.inv if i == 0 else s.row)(2 + i, " %-20s %4ddBm" % (who, rssi))
s.row(ROWS - 1, "SEL OCT+:connect OCT-:back", DIM); shots.append(s.save('07-scan.png'))
# 08 usb
s = Screen(); menu(s, "USB", [("Cable", "yes", 'info'), ("Host", "enumerated", 'info'), ("USB MIDI", "on", 'toggle'), ("USB in/out", "12/1544", 'info')], 2)
shots.append(s.save('08-usb.png'))
# 09 filters
s = Screen(); menu(s, "MIDI filters", [("Drop clock (F8)", "on", 'toggle'), ("Drop active sense", "off", 'toggle'), ("Drop SysEx", "off", 'toggle'), ("Dropped", "4096", 'info'), ("", "", 'info'), ("Panic (all off)", "sending...", 'action'), ("Panic on link loss", "on", 'toggle'), ("Note collision guard", "on", 'toggle')], 5, toast="panic: all notes off")
shots.append(s.save('09-filters.png'))
# 10 display
s = Screen(); menu(s, "Display", [("Screen off after", "30 s", 'enum'), ("Invert colours", "off", 'toggle'), ("", "", 'info'), ("Keys and MIDI jobs work with", "", 'info'), ("the backlight off.", "", 'info')], 0)
shots.append(s.save('10-display.png'))
# 11 system (Crashes in the warn colour: a watchdog reset has been seen)
s = Screen(); menu(s, "System", [("Firmware", "FM-1_931", 'info'), ("Version", "v0.2.0", 'info'), ("Built", "Oct 24 2026", 'info'), ("Battery", "3.87V raw 600", 'info'), ("Uptime", "0:12:41", 'info'), ("TRS in/out", "1532/0", 'info'), ("USB in/out", "12/1544", 'info'), ("BLE in/out", "0/1532", 'info'), ("Input drops", "0", 'info'), ("Boots", "17", 'info'), ("Last reset", "power on", 'info'), ("Crashes", "wdt 1 hang 0", 'warn'), ("Reset crash counters", "", 'action'), ("", "", 'info'), ("Reboot", "", 'action'), ("USB recovery mode", "", 'action'), ("Reset settings", "", 'action'), ("Input test", None, 'submenu')], 12)
shots.append(s.save('11-system.png'))
# 12 confirm: starts on "no"
s = Screen(); s.title("Confirm"); s.row(3, "Reset all settings?"); s.row(6, "  yes"); s.inv(7, "  no"); s.row(ROWS - 1, "SEL:choose OCT+:ok OCT-:no", DIM)
shots.append(s.save('12-confirm.png'))
# 14 keyboard: the cursor starts on Octave
s = Screen(); menu(s, "Keyboard", [("Keys send MIDI", "on", 'toggle'), ("Channel", "1", 'enum'), ("Octave", "+1  (F4..G6)", 'enum'), ("Velocity", "100", 'enum'), ("Velocity source", "fixed", 'enum'), ("Program (PRESETS)", "12", 'enum'), ("Bank MSB (ALGO)", "0", 'enum'), ("", "", 'info'), ("KNOB1", "CC1 = 0", 'info'), ("KNOB2", "CC74 = 64", 'info'), ("KNOB3", "CC71 = 0", 'info'), ("KNOB4", "CC7 = 100", 'info'), ("", "", 'info'), ("Knobs send CCs on this page.", "", 'info'), ("Hold OCT-/+: octave anywhere.", "", 'info')], 2, "OCT-/+:adjust  SAVE:back")
shots.append(s.save('14-keyboard.png'))
# 15 route options
s = Screen(); menu(s, "TRS -> BLE", [("Channels", "all", 'enum'), ("Remap channel", "to 10", 'enum'), ("Clock / transport", "off", 'toggle'), ("SysEx", "on", 'toggle')], 1)
shots.append(s.save('15-route-options.png'))
# 16 inputs
s = Screen(); menu(s, "Inputs", [("TRS transpose", "+12", 'enum'), ("TRS velocity curve", "soft", 'enum'), ("TRS fixed velocity", "100", 'enum')], 1)
shots.append(s.save('16-inputs.png'))
# 17 button MIDI
s = Screen(); menu(s, "Button MIDI", [("FX", "none", 'enum'), ("SCL", "none", 'enum'), ("ENV", "note 36", 'enum'), ("LFO", "none", 'enum'), ("EDIT", "none", 'enum'), ("SEQ", "program 5", 'enum'), ("ARP", "none", 'enum'), ("PLAY", "CC 64", 'enum'), ("REC", "CC 65", 'enum'), ("", "", 'info'), ("Key (PRESETS picks)", "C4: CC 66", 'enum'), ("", "", 'info'), ("OCT+ cycles the kind, KNOB1", "", 'info'), ("sets the number. Channel:", "", 'info'), ("the keyboard's. Or press", "", 'info'), ("a key to pick it.", "", 'info')], 10)
shots.append(s.save('17-button-midi.png'))
# 18 presets: one row per slot with a summary, or its name when the web editor gave it one
s = Screen(); menu(s, "Presets", [("Preset 1", "5/6 rt  ch 1", 'action'), ("Preset 2", "Live rig", 'action'), ("Preset 3", "empty", 'action'), ("Preset 4", "empty", 'action'), ("", "", 'info'), ("OCT+ loads (saves into an", "", 'info'), ("empty slot); hold OCT+ for", "", 'info'), ("load / save here / clear.", "", 'info'), ("rt = routes on, ch = keyboard", "", 'info'), ("channel. Pairing is not saved.", "", 'info')], 1, "OCT+:load/save hold:more")
shots.append(s.save('18-presets.png'))
# 19 composer: favourites on one row each
s = Screen(); menu(s, "Composer", [("Kind", "CC", 'enum'), ("Channel", "1", 'enum'), ("Data 1", "64 sustain", 'enum'), ("Data 2", "127", 'enum'), ("To", "USB+BLE", 'enum'), ("Send", "1 CC64 sustain 127", 'action'), ("", "", 'info'), ("Fav 1", "1 NoteOn C4 100", 'action'), ("Fav 2", "SysEx inquiry", 'action'), ("Fav 3", "empty", 'action'), ("Fav 4", "empty", 'action'), ("", "", 'info'), ("Fav: OCT+ sends, hold OCT+", "", 'info'), ("stores the message above.", "", 'info')], 5, "KNOB1:value OCT+:send/cycle")
shots.append(s.save('19-composer.png'))
# 20 status: the home page is a dashboard, not a menu: big links, two tables, a band at both ends
s = Screen(); s.title("Status"); s.big(0, 1, "USB ok ", OK); s.big(7, 1, "BLE ok  ", OK)
s.row(5, "         TRS   USB   BLE   KBD", DIM); s.row(6, " in    *1532    12     0    40"); s.row(7, " out       -  1544 *1532     -")
s.row(9, " Routes   to USB    to BLE", DIM)
s.row(10, " from TRS on        on noclk"); s.row(11, " from USB -         on", DIM, BG, 20, TXT); s.row(12, " from BLE off       -", DIM); s.row(13, " from KBD on ch map on")
s.row(14, " filters clk  drop 4096", DIM); s.row(15, " bat [###.]  ECO", DIM); s.row(ROWS - 1, " OCT+ menu   hold HOME perform", TITLE_ST, TITLE_BG); shots.append(s.save('20-status.png'))
# 21 decoded log, held 4 lines back
dec = ["TRS 1 NoteOn C4 100", "TRS 1 NoteOff C4 0", "TRS 1 NoteOn E4 100", "USB 1 CC7 volume 127", "BLE 1 NoteOn C5 80", "BLE Clock", "TRS 1 CC1 mod 34", "TRS 1 Bend +0", "USB SysEx inquiry", "TRS 1 NoteOn G4 112", "TRS 1 Program 6", "BLE 1 NoteOff C5 0", "TRS Clock", ">USB 1 CC64 sustain 127", "USB Start", "TRS 1 NoteOff E4 0"]
s = Screen(); log_page(s, dec, held="^4"); shots.append(s.save('21-midi-log-decoded.png'))
# 22 statistics
s = Screen(); s.title("Statistics")
for i, (k, v) in enumerate([("notes", 1532), ("CC", 212), ("program", 3), ("bend", 88), ("aftertouch", 0), ("clock", 48211), ("transport", 4), ("SysEx", 2), ("other", 0)]):
    s.row(1 + i, " %-11s %10d" % (k, v), TXT if v else DIM)
s.row(10, " notes C2..G6  vel 31..127"); s.row(11, " clock tempo 120.0 bpm"); s.row(12, " ch " + "".join("1234567890abcdef"[i] if i in (0, 2, 9) else "." for i in range(16)), DIM); s.row(13, " (1-9, 0 = 10, a-f = 11-16)", DIM); s.row(ROWS - 1, "OCT+:reset  OCT-:back", DIM)
shots.append(s.save('22-statistics.png'))
# 23 recorder
s = Screen(); menu(s, "Recorder", [("State", "PLAY 41/120", 'info'), ("Record", "start", 'action'), ("Play", "stop", 'action'), ("Loop", "on", 'toggle'), ("Play to", "USB+BLE", 'enum'), ("Clear", "", 'action'), ("", "", 'info'), ("Records what the inputs send", "", 'info'), ("(TRS, USB, BLE), up to 2048", "", 'info'), ("events. REC toggles recording.", "", 'info')], 2)
shots.append(s.save('23-recorder.png'))
# 24 reference
s = Screen(); s.title("MIDI reference")
ref = ["STATUS BYTES (n = channel 0-F)", " 8n note off   note  vel", " 9n note on    note  vel", "    (vel 0 = note off)", " An poly AT    note  pres", " Bn control    cc#   value", " Cn program    prog", " Dn chan AT    pres", " En pitch bend lsb   msb", "    (00 40 = centre)", " F0 .. F7      SysEx", " F1 MTC  F2 song pos  F3 sel", " F6 tune request", " F8 clock (24 per quarter)", " FA start FB continue FC stop"]
for i, l in enumerate(ref): s.row(1 + i, l, OK if l and l[0] != ' ' else TXT)
s.row(ROWS - 1, "SEL:scroll  OCT-:back", DIM); shots.append(s.save('24-reference.png'))
# 25 clock master
s = Screen(); menu(s, "Clock master", [("Clock", "running", 'action'), ("Tempo (bpm)", "120", 'enum'), ("Send to", "USB+BLE", 'enum'), ("", "", 'info'), ("Sends F8 at 24 per quarter,", "", 'info'), ("Start on run, Stop on stop.", "", 'info'), ("OK, or PLAY without a job.", "", 'info')], 1)
shots.append(s.save('25-clock-master.png'))
# 13 input test
s = Screen(); s.title("Input test"); s.row(1, " cells: 7,1 0,0"); s.row(2, " held: HOME", OK)
for i, (n, v, r) in enumerate([("SELECT", 12, "00"), ("ALGO", 0, "00"), ("KNOB1", -3, "11"), ("KNOB2", 0, "00"), ("KNOB3", 0, "00"), ("KNOB4", 0, "00"), ("PRESETS", 1, "00")]):
    s.row(4 + i, " %-8s %s%d   rest %s" % (n, "" if v < 0 else "+", v, r))
s.row(12, " last: SELECT +1", DIM); s.row(13, " frames 41203  drops 0", DIM); s.row(ROWS - 1, "HOME:leave", DIM); shots.append(s.save('13-input-test.png'))
# 26 minimal build status screen (app/minimal.c, make MINIMAL=1): no menu, all lines white
s = Screen(); s.row(0, " FM-1 MIDI bridge  MINIMAL"); s.row(1, " version 0.2.0"); s.row(2, " Oct 24 2026")
s.row(4, " Install build: USB MIDI,"); s.row(5, " TRS in, updater, editor."); s.row(6, " No Bluetooth, no keys.")
s.row(8, " USB: ready"); s.row(9, " TRS in 1532  USB in 12"); s.row(10, " USB out 1544")
s.row(12, " boot 3  power on"); s.row(13, " wdt 0 hangs 0"); s.row(14, " up 418 s"); shots.append(s.save('26-minimal.png'))
# 27 perform: the big font
s = Screen(); s.big(0, 0, "USB ok ", OK); s.big(7, 0, "BLE ok  ", OK); s.big(0, 1, "ECO", DIM)
s.big(0, 2, "Oct +1  "); s.big(9, 2, "Ch 1 "); s.big(0, 3, "Vel 100    ")
s.big(0, 5, "TRS", OK); s.big(4, 5, "USB", DIM); s.big(8, 5, "BLE", OK); s.big(0, 6, "KBD", DIM)
s.big(0, 7, "Bat [###.]", DIM); s.row(ROWS - 1, "OCT-/+:octave  HOME:leave", DIM)
shots.append(s.save('27-perform.png'))
# 28 a preset's page (hold OCT+ on its row): Load and Clear only on a used slot
s = Screen(); menu(s, "Preset 1", [("Name", "-", 'info'), ("Holds", "5/6 rt  ch 1", 'info'), ("Load", "", 'action'), ("Save here", "", 'action'), ("Clear", "", 'action')], 2)
shots.append(s.save('28-preset-slot.png'))
# 29 a toast after an action
s = Screen(); menu(s, "Composer", [("Kind", "CC", 'enum'), ("Channel", "1", 'enum'), ("Data 1", "64 sustain", 'enum'), ("Data 2", "127", 'enum'), ("To", "USB+BLE", 'enum'), ("Send", "1 CC64 sustain 127", 'action'), ("", "", 'info'), ("Fav 1", "1 CC64 sustain 127", 'action'), ("Fav 2", "SysEx inquiry", 'action'), ("Fav 3", "empty", 'action'), ("Fav 4", "empty", 'action'), ("", "", 'info'), ("Fav: OCT+ sends, hold OCT+", "", 'info'), ("stores the message above.", "", 'info')], 7, toast="stored in Fav 1")
shots.append(s.save('29-toast.png'))
# 30 the log held
s = Screen(); log_page(s, raw, held="PAUSED"); shots.append(s.save('30-midi-log-paused.png'))
# contact sheet
cols = 4; rows = (len(shots) + cols - 1) // cols; pad = 10; sw = W * scale
sheet = Image.new('RGB', (cols * (sw + pad) + pad, rows * (sw + pad) + pad), (40, 40, 40))
for i, im in enumerate(shots):
    sheet.paste(im, (pad + (i % cols) * (sw + pad), pad + (i // cols) * (sw + pad)))
sheet.save(os.path.join(out_dir, 'all-pages.png'))
print("wrote", len(shots), "screens to", out_dir)
