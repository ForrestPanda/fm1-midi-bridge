# Plan

The working log of the project, in the order things were decided. Entries keep the status
they had when written unless marked otherwise; what is verified on the hardware is in
`CHANGELOG.md` and `recovery.md`, and the open items are collected under "Open" at the end of
"Verify on hardware (UI)" below.

## Milestones

0. **Research on the SDK tree (no hardware needed).** Answer the questions below and
   write them into the docs.
1. **Build an SDK app for the FM-1 board.** Clone `apps/demo/demo_ble` into `app/`,
   write a board file with the FM-1 pins, build to `app.bin`, package to a `.fwsc`
   the Felucca installer accepts. Nothing flashed yet.
2. **First boot (hardware).** Blink an LED / draw on the LCD from the SDK, confirm
   the update entry works *before* anything else: install, then install again,
   then install stock. Only then continue. **Done 2026-10-19** on the first unit (the record:
   `recovery.md`, "The bridge on the first unit"): minimal build, stock back through our updater
   after the USB ring fix, then the full bridge, which boots with BLE advertising and the LCD up.
3. **USB MIDI device** on the SDK's USB stack (it has HID/CDC/audio classes; MIDI
   may need a custom class descriptor). Loopback test with a computer.
4. **TRS MIDI IN** on UART1 (PH8), parser, forward to USB. Test with a keyboard.
5. **BLE MIDI peripheral**: GATT service, advertising, packetizer. TRS -> BLE and
   USB <-> BLE. Test with iOS/macOS/Android MIDI apps.
6. **UI**: a status screen (connections, activity), HOME to start pairing, a way to
   forget bonds. Battery indicator.
7. Optional: BLE central mode (pair a BLE keyboard, forward to USB / TRS has no OUT).

## Answered on 2026-10-11 (details in `sdk-notes.md`)

- The SDK BLE demo builds on Linux with the Felucca-installed toolchain
  (`make TOOL_DIR=~/.jieli/toolchain/pi32v2/bin`). Only the post-build packaging
  needs JieLi's Windows/internal tools; we package ourselves.
- `demo_ble` app image with the BLE stack: 283 KB. FM-1 app slot: 581 KB. Fits.
- Update framework: `update.a` + `update_op_api_t` transport hooks (`uart_update.c`
  is the source model). Step 1 can be vendor code over a SysEx transport shim.
- BLE GATT server template: `apps/common/ble/le_trans_data.c`; ATT DB is a BTstack
  style byte array. Central mode exists too.
- USB: composite device with HID/CDC/UAC/MSD classes, **no MIDI class**; `hid.c` is
  the template to write one. MIDI descriptor constants exist in `uac_audio.h`.
- UART: `uart_open()` with `rx_pin = IO_PORTH_08`, `baud = 31250`.

## Decisions (2026-10-11)

- **USB MIDI: the SDK's USB device stack with a new MIDI class** (`app/usb/usb_midi.c`,
  written after the SDK's `cdc.c` / `hid.c`, Apache-2.0). Every open FM-1 firmware with
  USB MIDI (Felucca, SLOOP, FiMba, FoMni, x0x) uses Felucca's `usb.c`: GPL-3 and
  bare-metal on the MUSB registers. It cannot be linked with the closed JieLi
  Bluetooth libraries in a firmware that is distributed, and it would fight the SDK's
  own USB driver (in `cpu.a`) for the controller. ip2k's MIT "Lunar Modulator" is SDK
  based but has no USB MIDI yet (roadmap item).
- **Packaging and install: Felucca's tools, run as programs.** `tools/package.sh`
  calls `fm1pkg_make.py` with our `app.bin` and Felucca's update loader;
  `tools/install.sh` calls `fm1_install.py`. GPL tools used at build time put
  nothing GPL into the firmware; the loader is a separate program the SPL runs
  (shipped in the package with its source available in the Felucca checkout).
  MIT alternatives exist for inspection and unpacking (ip2k `tools/jieli/inspect_fwsc.py`,
  kagaimiq's jl-misctools) if we ever want a GPL-free toolchain.
- **Routing matrix** in `app/midi/router.c`: TRS->BLE, TRS->USB, USB->BLE, BLE->USB,
  each switchable, stored as one byte. The on-device UI for it is milestone 6.
- **VM region ends at 0xE0000.** `tools/fm1pkg_make.py` is our fork of Felucca's
  packager (GPL-3 build tool): the VM region descriptor is 0x93000-0xE0000 instead
  of 0x93000-0xE9000, so the SDK's VM driver (bonding keys, settings) can never touch
  the loader staging area 0xE0000-0xE5000 or the update record at 0xE4F00. Verified
  by decoding the packaged tables. The region descriptors live in the app directory
  at 0x4000, inside the app area every install writes, so the change applies on any
  install of our package, from stock or from an earlier build. The head (SPL, config,
  key block) is never written by our installs.
- **Never write the flash head.** ip2k's notes: the FM-1's SPL is SDK V1.1.9's
  `uboot.boot` (14,384 bytes); our SDK V1.2.1 ships a different one (14,536 bytes).
  Felucca's loader only writes the app area, so the SPL in the package is inert.
  Keep it that way.

## Built so far (host-tested where possible; on the first FM-1 since 2026-10-19)

- `app/bt_ble/ble_midi.c`: the SDK GATT server glue serving the BLE MIDI service
  (`midi_profile.h`), advertising the 128-bit service UUID, name "FM-1 MIDI",
  bonding on. API: `ble_midi_send`, `ble_midi_set_rx`, `ble_midi_ready`.
- `app/usb/usb_midi.c`: USB-MIDI class on the SDK USB stack, started on plug-in by
  the SDK's OTG detection (`task_pc.c`, `USB_PC_NO_APP_MODE`).
- `app/bridge.c`: the bridge task. UART1 on PH8 (then the SDK uart device "uart1"; register
  level in `app/trs_uart.c` since 2026-10-19, 31250)
  -> parser; USB and BLE input queued from their contexts; router fan-out; BLE
  packets batched per burst; update-protocol SysEx intercepted and handed to
  `fm1_update`.
- `app/update/fm1_update.c` + `fm1_update_port.c`: update step 1 (handshake
  identity, upgrade command, package reads, loader staging at 0xE0000, host
  confirmation, UPDATA_PARM at 0xE4F00 + RAM, reset) and the soft key to the
  mask-ROM USB mode. `tests/test_update.c` runs a whole session against the real
  `build/fm1bridge.fwsc` with a simulated installer and flash.
- `app/ui/lcd.c` + `midilog.c`: the panel (then the SDK SPI device "spi1", register level since
  2026-10-19; port C, D/C on
  PC8 as GPIO, backlight PA2) and a logger screen: status line (USB, BLE, enabled
  routes) and the last 16 messages from any port, drawn at most 20 times a second
  from the bridge task. Every message the router sees is logged (first SysEx chunk
  only). The 8x14 font is generated from DejaVu Sans Mono.
- `app.bin` 310 KB of 581 KB then (363 KB on 2026-10-24, README); `tools/package.sh` -> `build/fm1bridge.fwsc`.

## UI (milestone 6, built 2026-10-11; on the unit since 2026-10-19)

- `app/input/`: own matrix scanner. TIMER3 at 10 kHz (`-DMX_TICK_US` overrides), one
  column per tick, rows read the tick after latching, 1.1 ms frames; debounce 8 frames;
  encoders with a continuously learned rest state, clockwise = B closes first
  (Felucca's hardware-tested sign, fm1-emu's too; the first cut had it the other way
  round); 32-event queue; button LEDs. `tests/test_matrix.c`.
- `app/ui/`: UI task (prio 8) owns the panel; row cache; generic menu renderer; pages:
  splash, main (status footer with battery mV), Routing, MIDI log, Bluetooth (pairable,
  disconnect, forget bonds, host mode, scan, pair on connect), USB (cable, enumerated,
  USB MIDI switch), MIDI filters (clock / active sense / SysEx, panic), Display
  (backlight timeout, invert), System (identity, version, build date, battery, uptime,
  counters, input drops, reboot, USB recovery, reset settings), confirm dialog.
  Navigation: SELECT/PRESETS scroll, OCT+ ok/toggle, OCT- back, HOME main menu, KNOB1
  adjusts. Example screenshots: `docs/ui-examples/` (`tools/ui_mock.py`).
- `app/settings.c`: versioned, CRC-checked struct in syscfg id 10, written 1 s after a
  change from the UI task. `tests/test_settings.c`.
- `app/midi/router.c` filters and `app/midi/panic.c` (2080 messages, 64 per bridge loop).
- `app/bt_ble/ble_host.c` + `ble_port.c` + `adv_parse.c`: exclusive host mode. Enter:
  advertising off, disconnect a phone, `ble_stack_gatt_role(1)`, our handlers, scan
  (active, 30 ms / 20 ms) filtered on the MIDI service UUID, list of 8 devices; connect
  (15 ms interval), `ble_op_search_profile_all`, record the MIDI characteristic value
  handle, subscribe (CCCD 0x0001), notifications -> bridge BLE port, sends as
  write-without-response. Exit: back to the peripheral through `ble_midi_role_peripheral()`.
  The stored mode is applied at BT init (`ble.c`). `tests/test_adv_parse.c`.
- BLE link profiles (Bluetooth page, "Link"): low latency = 7.5 ms requested, 15..30 ms
  fallback for Apple, no slave latency; power saving = 30..50 ms with slave latency 4.
  Hold GLO on any page to toggle power saving (GLO LED lit = power saving, "ECO" in
  the main menu footer). Slave latency only delays phone -> FM-1 traffic; the FM-1's own sends still go out at
  the next event, so page-turn use from a TRS controller stays prompt in power saving.
- Keyboard mode (`app/midi/keys.c`, Keyboard page): the 27 keys send note on/off
  (F3 = 53, octave -3..+3 via OCT-/OCT+ on the page, fixed velocity, channel 1..16) from
  any page; the four knobs send CC 1 / 74 / 71 / 7 with absolute values while the
  Keyboard page is open (KNOB1 belongs to the menus elsewhere). Router port KBD with
  routes to USB and BLE, on by default. Key LEDs light while held. SAVE leaves the page.
- Per-route options (Routing > Options ...): channel filter (all / one channel on the
  panel, any mask from the editor), channel remap, clock/transport pass, SysEx pass.
  Inputs page: TRS transpose and velocity curve (linear / soft / hard / fixed).
- Button MIDI page: FX, SCL, ENV, LFO, EDIT, SEQ, ARP, PLAY, REC each send nothing, a
  note, a CC (127 on press, 0 on release) or a program change, on the keyboard
  channel. PLAY defaults to CC 64 as a page-turn pedal substitute. A mapped button no
  longer reaches the UI; HOME, GLO, OCT-, OCT+, SAVE and SELECT stay the UI's. Any of
  the 27 note keys can also take a job instead of its note (Button MIDI > Key: PRESETS
  or a key press picks the key). Mapped buttons and keys work with the keyboard off.
- Keyboard page: PRESETS encoder sends program changes, ALGORITHM sends bank MSB.
- Activity LEDs: REC blinks on USB traffic, PLAY on BLE traffic (unless mapped).
- Web editor (`web/editor.html`, `app/editor.c`): all settings over USB-MIDI SysEx,
  `docs/web-editor.md`. Also a second MIDI monitor in the browser.
- Learning-tool pages: Composer (any message by kind/channel/data or raw bytes, to
  USB/BLE/both, auto note off, 4 favourites in settings v8), Connections (ports,
  counters, activity, routes with their options), decoded log view (OCT+ on the log
  page; `app/midi/decode.c`). Editor command 0x09 sends raw bytes to a port; the web
  monitor decodes incoming messages.
- Statistics page (`app/midi/stats.c`: counts by kind and channel, note/velocity
  range, tempo from clock), Recorder page (`app/midi/recorder.c`: 2048 timed events,
  loop, play to USB/BLE), MIDI reference page, inquiry reply decoding with a
  manufacturer table.
- Clock master page (bridge-loop accumulator, F8 at 24 ppq, Start/Stop), note
  collision guard on both outputs (`app/midi/merge.c`), MASTER pot as keyboard
  velocity (ADC PB6), host-mode auto reconnect to the stored peer (`ble_host_tick`).
  Settings v9.
- Robustness: `app/health.c` keeps boots, reset reason, watchdog resets and bridge
  stalls in syscfg id 15 (System page); the UI task restarts the chip if the bridge
  loop stops for 5 s (the SDK's own 4 s hardware watchdog covers the system task).
  Web editor: settings export/import as JSON, firmware install with the installer
  protocol (unverified on hardware).
- `app.bin` see the footprint in README.
- 2026-10-12, hardware readiness: the device log (`app/fm1_log.c`, 4 KB ring, the
  SDK `log_putbyte` hook, editor commands 0x0A/0x0B, web editor Device log card and
  sysinfo rows: heap, task stack margins, health counters); the minimal build
  (`make MINIMAL=1`, `app/minimal.c`, identity FM-1_930, `tools/package.sh --minimal`);
  fuzz harnesses under ASan/UBSan for every parser the bridge task runs
  (`tests/fuzz`, 400k iterations each clean, update entry at 96% line coverage);
  `tools/fm1_recover.py` (soft key, SDK vendor request, wait for UBOOT),
  `tools/fm1_flash.sh` (jl-uboot-tool dump/verify/restore), `docs/recovery.md`;
  USB strings M-VAVE / FM-1 (`app/usb/descriptor.c`). Tagged v0.1.0.
- 2026-10-12, the bridge task under test: `tests/host/` stands in for the SDK headers
  and ports (RAM rings with the library's all-or-nothing contract, read from its
  bitcode), `bridge_step()` is the loop body; `tests/test_bridge.c` covers the three
  inputs, routes and filters, the SysEx hand-over to the updater and the editor, the
  inject ring, panic, link-drop panic, the clock master, the recorder, the collision
  guard, a full USB ring and a failed UART; `tests/fuzz/fuzz_bridge.c` scripts the
  same simulator from mutated input. Found and fixed: an over-long SysEx with one of
  our prefixes used to leak its tail chunks to the router. `tests/test_update.c` now
  runs full -> minimal -> full against both device identities. `tests/fuzz/soak.sh`
  for long runs with a fresh seed; the driver saves a crashing input for replay.

- 2026-10-16, UI pass (all of it walked in fm1-emu, `tools/ui_walk.py`, 59 runs):
  - Two bugs: the backlight wake swallowed the first press after a dark pause (a note
    key or the PLAY page turn did nothing); MIDI jobs now run before the wake check.
    The Keyboard page had no way to change its own rows (KNOB1 sends CC 1 there and
    OCT-/+ shifted the octave): OCT-/+ now adjust the highlighted row, the cursor
    starts on Octave, held OCT-/+ shift the octave from any row.
  - Navigation: the cursor stays where it was when a page is re-entered by OCT- or a
    closed dialog (`ui_came_back()`, `ui_menu_enter()`); the cursor skips INFO rows and
    blank separators; hold OCT+ (700 ms) is a row's second action (`ui_item.hold`:
    a route's options, a preset's page, storing a favourite, holding the log); KNOB1..4
    accelerate (gap < 40 ms x10, < 100 ms x3, `accelerate()` in `ui.c`; pages that add
    the delta get it, pages that look at its sign do not); the confirm dialog starts
    on "no" and SELECT reaches "yes".
  - The title row is a dark blue band (`UI_C_TITLE_BG`, status in `UI_C_TITLE_ST`),
    the cursor the white bar: before, both were black on white and looked alike.
  - The home page (Status) must not look like a menu: USB/BLE in the big font, the
    counters and the routes as tables (a star on a counter that moved), a footer band
    to match the title band. Its 300 ms refresh and the menu's battery refresh go
    through the row cache (`ui_refresh`), not a full repaint.
  - Glanceability: the Status page (formerly Connections) is the home page (after the splash, HOME from
    anywhere, the root of OCT-; HOME there opens the menu; its LED means home); every
    title row ends with "U:.. B:.." (`ui_title()`); toggles green/dim (`ui_row2()`,
    one colour split per row in the cache); `UI_C_WARN` on low battery, crashes and
    input drops (`ui_item.warn`); `ui_toast()` on the footer for 1.2 s after actions;
    the Perform page (`PAGE_PERFORM`, hold HOME) in a 16x28 DejaVu font
    (`app/ui/font_big.h`, `mkfont.py --size 24 --w 16 --h 28 --name BIG`,
    `lcd_text_big()` drawn in two 14-row passes through the existing strip).
  - Smaller: the main menu in three groups (Setup / Bench / Device, Presets moved up
    from System); the Routing page is one row per route with the option marks; the
    MIDI log keeps 64 lines, SELECT scrolls back ("^N"), hold OCT+ holds the view
    ("PAUSED"); presets show a summary of what they hold (`settings_preset_peek()`);
    battery as a 4-cell bar with uncalibrated thresholds (4.0 / 3.8 / 3.65 / 3.5 V at
    the pin, `BAT_*_MV` in `ui.c`); a zeroed composer favourite reads "empty".
  - Found by the walk pictures: the Keyboard page compared the event id with
    `MX_BTN_OCT_MINUS` (0) without the type, which is also `MX_ENC_SELECT`, so SELECT
    turns were swallowed there. The SDK's printf also lacks `%.*s` and `%-23.23s` as far
    as we know, so no precision forms are used (padding by hand).

### Verify on hardware (UI)
- **Verified 2026-10-20 (the user):** encoder direction and acceleration, the 700 ms holds,
  the backlight timeout and the wake-up press, the Perform page's big font at a metre.
- **Verified 2026-10-20 (the user, every page by hand):** splash then the Status page; HOME opens the menu; SELECT scrolls, clockwise =
  down (if it is up, the encoder sign in `matrix.c` `qtab` is the other way round on
  real parts); OCT+/OCT- navigate; HOME returns to the Status page from any page.
- Battery: until 2026-10-20 PB1 was never set up as an ADC input and read 0.00 V. Now Felucca's
  and SLOOP's raw thresholds (531 / 561 / 591, ours 611 for the fourth cell, `BAT_*_RAW` in
  `ui.c`); fully charged on USB the unit read raw 619, 2042 mV at the pin, so a 1:2 divider and a
  4.07 V cell.
  No meter here: the cell voltage and where orange "low" falls against the SDK's own cut-off
  stay unchecked (dropped for now, 2026-10-20, the user's call).
- (Verified, above.) Encoder acceleration feel: a deliberate turn should step by 1, a quick one by 3, a
  flick by 10 (the gaps are measured by the UI task, `accelerate()`); retune the 40 and
  100 ms thresholds if real detents land differently.
- (Verified, above.) Hold timing: 700 ms (`MX_HOLD_FRAMES`) for hold OCT+, hold HOME, hold GLO, hold
  OCT-/+ on the Keyboard page; the release after a hold does nothing.
- With the backlight off, a note key, a mapped button and a knob on the Keyboard page
  send their MIDI and light the panel; a menu button only lights the panel. **Verified
  2026-10-20 (the user).**
- (Verified, above.) The Perform page's big font on the real panel: legibility at a metre, the 16 px
  strip under the eighth row holds the footer.
- The 10 kHz TIMER3 ISR does not disturb TRS MIDI, USB or BLE (log page still shows a
  TRS keyboard while scrolling menus). If TIMER3 is taken, use TIMER2 (`matrix_port.c`).
  **Verified 2026-10-20:** three runs of the TRS suite (`tools/trs_test.py`, 30 of 30) while the
  user scrolled the menus, the log and Statistics: no loss, no lag; menus under USB traffic the same.
- TRS MIDI IN through `app/trs_uart.c` (register level, Felucca's set-up): **verified
  2026-10-20** with `tools/trs_tester/` (`tools/trs_test.py`, ten tests, USB and BLE): TRS to USB
  median 0.8 ms from the last byte on the line, to BLE 5-9 ms; with nothing on the jack, no UART
  interrupts (the log's `trs:` line stays still). Still to look at: the Statistics page itself.
- The FM-1's own keys reach USB/BLE with nothing plugged into TRS (the bridge loop used
  to wait for TRS bytes; it now sleeps on a semaphore every producer posts).
- Matrix tables (`app/input/matrix.c`): System > Input test shows every closed cell as
  (column,row), the decoded button, the encoder counts and learned rest states. Press
  every button and turn every encoder; fix the tables for any mismatch (batches differ).
- Debug console: the UART2 TX pin is -1 (nothing driven) until PC3 is confirmed free.
- Routes and filters survive a reboot; backlight times out and any key wakes it. **Verified
  2026-10-20** (`CHANGELOG.md`).
- Panic silences a stuck note on USB and BLE. **Verified 2026-10-20** (2080 messages to each).
- USB page follows plug/unplug; "USB MIDI off" stops enumeration. **Verified 2026-10-20.**
- Forget bonds forces a phone to re-pair. **Verified 2026-10-20** (the PC as the phone).
- Host mode: **verified 2026-10-19** against a BLE MIDI peripheral made of the PC's Bluetooth
  adapter (`tools/ble_midi_peripheral.py`, BlueZ): the scan lists it, it connects (7.5 ms,
  MTU 200), discovery finds the MIDI characteristic, the subscription brings its notes to USB,
  a note sent from the FM-1 reaches it, a dropped link reconnects by itself, "Pair on connect"
  pairs (just works, encrypted before discovery), host mode off makes the FM-1 advertise again
  and on again at runtime reconnects. The role switch at runtime, the hook names and the
  `adv_report_t` offset were right. What was wrong (fixed): the stack reports 128-bit UUIDs
  big-endian, as written (the little-endian form matched nothing); a create connection with no
  scan before it never completed (the SDK's demo connects from an advertisement, so does the
  reconnect now); `ble_op_conn_param_request` is the slave's L2CAP request and reset the chip
  when the central used it on a settings write (`ble_op_conn_param_update` is the central's);
  a settings write from the editor drove the panel from the bridge task (the UI task applies
  it now); and a USB bulk write with no reader on the PC blocked the bridge loop for good (a
  link-drop panic burst, two stall resets): the IN FIFO is checked first. Then a real one, the
  same evening: a keyboard through a Casio WU-BT10 adapter (BLE address 6C:47:60:xx:xx:xx, random,
  one above its classic address) in pairing mode. It advertises the MIDI UUID
  alone, no name anywhere, no GAP name characteristic (its GATT: the MIDI service and two
  "CHAR" vendor characteristics), so the Scan page shows its address; connect, subscribe at
  handle 3, no pairing asked, no parameter request from it, its notes reach USB with their
  velocities, and notes sent from the FM-1 go out to it. An earlier attempt at -83 dBm dropped
  with 0x3E during discovery. Heard 2026-10-20: the keyboard sounds what the FM-1 sends.
- PC8 reclaimed as D/C after the SPI driver opened port C; panel init at 12 MHz.
- The SDK log hook: yes, the vendor libraries' output lands in the device log through
  `log_putbyte()` (`[Info]: [VM]...`, `[LL_E]...`, `PAIR_PROCESS` lines, seen 2026-10-20).
- Stack margins in the Device card after an hour of traffic (bridge and UI tasks). **Verified
  2026-10-20**: a 55-minute soak, margins and counters unchanged.
- `tools/fm1_recover.py`: the soft key on stock and on ours, the UBOOT disk's SCSI strings
  (`WL82 UBOOT1.00`, USB 4c4a:8057), jl-uboot-tool on WL82: **all verified 2026-10-19/20**.
- **Open:** the SDK vendor request (`fm1_recover.py --vendor`) and the System page's *USB
  recovery mode*; M-UPGRADE on top of the bridge (USB strings "FM-1", but JieLi's 3654:4155,
  not stock's 4c4a:c755); iOS and macOS; another unit's key matrix. (The Pairable change of
  2026-10-24, off surviving a disconnect, verified the same day.)

## First-boot checklist (when an FM-1 is here)

(Written before the unit came; walked 2026-10-19, `recovery.md`.) No stock round trip is
needed between our own installs: our package carries Felucca's loader, which writes the app
area (including the region descriptors) and never the head. Stock V15 is the recovery path,
not a prerequisite; step 5's stock install only proves that path once.

0. Build the D+/D- dongle (ip2k's `dongle/`), clone jl-uboot-tool, and take a full
   1 MB dump of the untouched unit: `tools/fm1_recover.py`, `tools/fm1_flash.sh dump`.
   The dump is the only backup of the unit's own data (BT address, BTIF). If the soft
   key does not get stock into UBOOT mode, the dongle does. Read `docs/recovery.md`.
1. `tools/install.sh --info` on stock: note the identity and that the handshake works.
   Note the USB VID/PID and product string of stock (`lsusb -v`) for `app/usb/descriptor.c`.
2. Back up: keep the stock `FM-1.fwsc`; confirm M-UPGRADE can reinstall it once.
3. Install `fm1bridge-minimal.fwsc` first (identity FM-1_930: no radio, no keys, no UI
   task). Expected: the installer's step 1 is served by our app (21 read requests on the
   unit), the loader appears as `ota-FM-1_900` (Felucca's), step 2 writes.
4. On boot: the status screen; USB enumerates as "FM-1" MIDI; `tools/install.sh --info`
   gets `FM-1_930`; the web editor connects, the Device log card shows the boot lines
   (and whether SDK printf output reaches the ring through `log_putbyte`), the Device
   card shows heap and stack margins; a TRS keyboard counts on the screen.
5. Install the minimal build again (proves our step 1 on hardware), then install
   stock back, then the full `fm1bridge.fwsc` (identity FM-1_931).
6. Only then: TRS keyboard -> BLE/USB, USB <-> BLE, soft key (`tools/fm1_recover.py
   --softkey`, then the vendor request). PC3 stays undriven; the SysEx log is the console.
   The logger screen shows every message as it passes, so a keyboard on TRS should
   print "TRS 90 3C 64" lines without any host attached. If the panel stays dark:
   check that the SDK SPI driver left PC8 usable as the D/C output (we reclaim it
   after `dev_open("spi1")`), and the 12 MHz clock.

## Before sharing (2026-10-20; status 2026-10-24)

Asked whether the bridge is ready for a post on the FM-1 subreddit: not as an "install this"
post, but as an alpha call for testers once these are done.

- **Publish.** Public at github.com/ForrestPanda/fm1-midi-bridge. A release `v0.2.0` with both
  `.fwsc` packages, their SHA-256 and the GPL source offer for Felucca's loader (a pinned
  Felucca commit), and `docs/install.md` as the short install. The packages need no build.
- **Going back to stock.** Felucca's installer with M-VAVE's `FM-1.fwsc`, verified 2026-10-19;
  the docs now say that, and that M-UPGRADE is untested on the bridge (its USB ids are not
  stock's). Still worth one try with M-UPGRADE.
- **More days on the unit**: done 2026-10-19..24 (Android phone, a BLE keyboard through the
  WU-BT10, the TRS tester with a clock, a 55-minute soak; the USB-silent episodes did not
  return). Not done: iOS, macOS, a second unit.
- **Docs review** (2026-10-24): the manual, recovery, install and protocol docs checked against
  the code and the scripts; the screenshots regenerated from the current pages.
- **The post itself**: alpha, one unit, five days on hardware, a soft-lock needed the mask ROM
  dongle once; take a dump first and read `docs/install.md`. The key matrix may differ by batch.

## MIDI log filters (asked 2026-10-20, built the same day)

Built as below (`settings.log_hide`, the byte that was `pad2[0]`, so no format change);
verified on the unit the same day: the buttons, their LEDs, the status line. Four show/hide selectors on the MIDI log page: notes, program changes, control changes, and
everything else (clock, SysEx, pitch bend, aftertouch, system). Asked for on KNOB1..KNOB4;
proposed instead: the FX, SCL, ENV and LFO buttons, one toggle each, the button's LED lit while
that kind is shown. A press is a clear on/off (a knob turn is relative, easy to bump, and shows
nothing), the four sit in a row, the log page does not use them yet, and KNOB1 stays the menu's
value knob everywhere. The filter applies to what the page lists (`midilog_add`), not to what
the bridge forwards; the state survives leaving the page (a field in the settings, so a
reboot keeps it too); the title line shows the active filter (e.g. "N P C -").

## Future work: a phone and a keyboard at once (2026-10-20)

Today BLE is one role at a time: the peripheral ("FM-1 MIDI", a phone connects) or host mode
(the FM-1 connects to a BLE keyboard), and entering host mode drops the phone. Both at once is
possible on the chip and the SDK, not in our build:

- The controller is built with every LE role (`lib_btctrler_config.c`: ADV, SCAN, INIT, SLAVE,
  MASTER), `ble_stack_gatt_role(2)` runs server and client together (host mode uses 1), and
  the SDK's `apps/common/ble/multi_demo/` (in our Makefile, off: `TRANS_MULTI_BLE_EN 0` in
  `app/include/app_config.h`) holds 2 peripheral and 2 central links with the per-connection
  `ble_op_multi_att_*` calls instead of `ble_op_att_*`.
- The work: `TRANS_MULTI_BLE_EN` with 1 slave, 1 master (controller slots and ACL buffers per
  link); `ble_midi.c` and `ble_host.c` share the stack and sort events by connection handle the
  multi demo's way, instead of taking it over in turn (the role switch in `ble_host.c` mostly
  goes); BLE becomes two ports in the router (`MIDI_PORT_COUNT`, keyboard to phone becomes a
  route), so the settings version, the editor protocol, the Routing page and the web editor
  follow; the Status page shows both links, the Scan page works with a phone connected.
- Costs: the two links share the radio (the 2026-10-20 burst figures in the changelog roughly halve, a few ms more
  latency with both busy; reconnecting the keyboard scans with the phone link up), a few KB of
  RAM (375 KB heap free). The risk is JieLi's closed library in a mode we have not run (it
  gave us the UUID byte order and the parameter-request reset).
- First step, before the routing: both links up on the unit (a phone on the peripheral, the
  PC's `tools/ble_midi_peripheral.py` as the keyboard), traffic on each, `tools/usb_to_ble.py`
  for the peripheral side's figures.

## Still open

Done by hand 2026-10-20 evening (the changelog): Panic, Statistics, menus under traffic, USB
MIDI off, Forget bonds, the soft key on our firmware, the web editor's Backup and Firmware cards,
the phone app's view of the PC to phone stream (MidiScope: MIDI Commander's log does not list
BLE ports).
The stale-bond fix: seen working with the PC (the changelog); the WU-BT10 cannot test it, it never
pairs. A peer that hangs up at encryption instead of answering 06: handled, not yet seen.
Also done: the Composer and the Recorder (2026-10-21; the Recorder's first try recorded nothing,
started with the REC button, most likely a MIDI job on this unit's Buttons page: the page's Record
and Play rows worked). Also done: preset names (the web page and the panel, the user); the panel woken by music keys, TRS in while the menus scroll; the battery now reads (it
read 0 V), its thresholds Felucca's, no meter to check them. Left for later (the user's call): iOS; M-UPGRADE; key-matrix tables of other batches.

- USB to BLE bursts: fixed 2026-10-20, see the changelog. What is left: a burst of 1200
  messages at once lost 2%, all on the USB side (the FM-1's USB in count was 22 short; the
  2048-byte ring, most likely), and the Status page's dropped figure adds BLE, USB and filter drops together, so a
  panic to a USB port nobody reads looks like BLE loss. `tools/usb_to_ble.py` measures it.
- TRS to BLE with a MIDI clock on: done 2026-10-20 (`tools/trs_test.py --ble`, the changelog).
- Host mode and a stale bond (2026-10-20): the FM-1 kept keys for the PC from an earlier
  pairing, the PC no longer had them; at connect the FM-1 asked for encryption, got status 06
  (key missing), then no discovery, and dropped the link after 15 s (`no MIDI subscription`).
  Any keyboard reset or told to forget the FM-1 would never work in host mode again until
  Forget bonds. Fixed the same day (the bond deleted, the peer reconnected: `unbond_retry` in
  `ble_host.c`), seen working the same night after two more fixes (the changelog). Still to see:
  the hang-up variant (13 at encryption, no 06), handled the same way.
- A phone's or tablet's MIDI app grabs any BLE MIDI device it sees: with the FM-1 in host mode, the
  phone connected to the PC's "PC MIDI" test keyboard and held it, and a tablet's Casio app holds
  the WU-BT10 (the FM-1 then connects but no discovery answers, 15 s, dropped). Turn their
  Bluetooth off for host-mode bench tests (2026-10-20).
- BLE timestamps and the clock master run on the 10 ms OS tick (`timer_get_ms()`): a receiver
  that plays by the timestamps gets a clock spread of 4.3 ms at 200 bpm (3 ms on arrival), and
  the clock master can only send on a tick. The key matrix's 10 kHz TIMER3 interrupt could keep
  a 0.1 ms count for both.
- With no program reading the PC's port, the first USB packet left behind stays in the IN FIFO
  and reaches whoever opens the port next (seen: the All Sound Off of a BLE disconnect's panic).
  A stale note-on would sound late; flush the FIFO when it has sat for a while.
- The device card's "stack free bridge 0" before the bridge task's first sample (a few seconds
  after boot) reads like an overflow: show "-" until sampled.
- A line break under 1 ms still reaches the parser as a 00 byte (the timeout check needs the line
  still low 1 ms after it).
- A soak against the PC's Bluetooth longer than ~45 minutes measures the AX200 / BlueZ more than
  the FM-1 (2026-10-20): power the adapter off and on (`bluetoothctl power off; ... on`) first.
- Twice on 2026-10-19 the unit stayed enumerated on USB but answered no SysEx until a power
  cycle; the log's tail before a stall reset is saved now (`FM1_STALLLOG_ID`), read the boot
  log after the next one. Not seen again in the whole of 2026-10-20 (a 55-minute soak, the TRS
  suite, about twenty installs): closed unless it comes back.
- USB VID/PID: stock is 4c4a:c755 (read 2026-10-19, `hardware.md`); `app/usb/descriptor.c`
  keeps JieLi's 3654:4155 with the M-VAVE / FM-1 strings, which every installer finds by name.
  Decide whether to copy stock's ids (M-UPGRADE may look for them).
- Whether the hardware-side details of the board file are right: the key matrix tables (may
  differ by batch), the codec lines, PB1 never driven. The LCD pins and the register path are
  verified (2026-10-19, `hardware.md`); the SDK's "spi1" device is not used for the panel.
- Debug UART: the SDK demo prints on UART2 TX = PC3. Every open FM-1 firmware
  leaves PC3 alone (Felucca drives PC0/1/2/6 for the codec and PC7..PC10 for the
  panel; nothing documents PC3, PC4, PC5) and there is no schematic, so "free" cannot
  be shown from the trees. The board file keeps `tx_pin = -1`; the device log over
  SysEx is the console. Only worth revisiting if a boot never reaches USB.
- jl-uboot-tool on the WL82 family and the soft key on stock: verified 2026-10-19
  (`docs/recovery.md`); the SDK vendor request is still open.
- **fm1-emu runs this firmware (2026-10-14)**, full and minimal builds, with the BLE
  stack on (`BLE:adv`): splash, menu, encoder, keys, TRS in, routing to USB and BLE.
  It took nine fixes to the emulator, on branch `ac79-sdk-firmware` of `../fm1-emu`
  (four commits on top of upstream master, not pushed; its author
  tuned it on Felucca-family firmwares and M-VAVE's; the AC79 SDK uses instruction
  forms and a lock encoding those never emit):
  - ISA: `sp = R` read the wrong register (`sp = r5` loaded r3); `{pc, regs} = [sp++]`
    with a register mask no corpus used; `R = rets/psr/cnum/...` and `sreg = R` for
    registers r8-r15; the 4-byte post-increment-by-register loads and stores
    (`h[rB++=rI] = R`, `de ec/ed/ee`), which two immediate-form classes also matched
    and ran as something else; dual-issue `clz #` and `clz` of r4-r7; the 4-byte
    `[R+-#i] = R` (`d7 ec`, a frame store the routing screen's `prvListTasksWithinSingleList`
    hits), whose offset the slot solver had given up on: same pieces as its load twin.
  - The hardware lock: `lockset`/`lockclr` (`41 00`/`40 00`, the SDK kernel's spelling)
    were no-ops; only the `lockset R` forms the stock firmware uses took the lock. With
    the two cores running the SDK's scheduler unlocked, a task that blocked on one core
    was re-assigned to it by the other and queued on a wait list twice (the log mutex);
    the boot stalled at ~250 ms in `vListInsert`, differently with every build.
  - TIMER0..3 with clock source 0 count lsb_clk (192 MHz), not the 24 MHz crystal:
    the matrix scan ran 8x slow.
  - `work/lcd.bmp` is rewritten only with `FM1_LCD_DUMP=1` (a long run rewrote it
    thousands of times); `FM1_TRACE_MEM=hex,hex` adds RAM words and `sp` to the trace.
  - Speed: it ran at 0.08x real time (the stock firmware 1.15x). cpu1 runs in slices
    between cpu0's, and the emulator ended a slice early whenever cpu1 came back to the
    same pc with the same registers ("a polling loop with nothing to do"). The SDK's
    idle loop (`idle`, then `wdt_clear` through the p33 byte lock) is such a loop, so
    cpu1 was always parked inside the p33 critical section while cpu0 ran, and cpu0
    spent 70% of its instructions spinning on `testset` in `p33_lock` (interpreted,
    never reaching `idle`, so no time was skipped). cpu1's slice now ends at its own
    `idle` instruction, the one place in that loop where it holds nothing: 0.93x real
    time, 92% of cycles skipped as idle. (`fm1-live --bench 3000` measures it;
    `FM1_BENCH_PCS=1` now samples cpu1's pc as well.)
  Stock, Felucca, SLOOP and baudgirl: byte-identical audio and stats before and after.
  How to run: `fm1-emu --fwsc build/fm1bridge.fwsc --ms 4700 --keys
  "2400:SELECT:+2,2700:OCTUP,3000-3200:n12,4000:midi:903c64" --snap 3800` (frames in
  `work/lcd_<ms>.bmp`); the device log is the `ring.N.0` symbol (`nm`, its `total` is
  `ring.N.1`: LTO splits the struct), `--dump` it. The batch tool has no idle skipping
  and runs this firmware at about a minute per emulated second; `fm1-live --fwsc ...
  --shot MS` with `FM1_BENCH_KEYS` (same key ids, `ms:id`, `ms:id^`, `ms:eN:clicks`,
  `ms:mHEX`) runs at 0.9x, reports a halt the same way and leaves the panel in
  `work/panel.ppm`; `FM1_DUMPRAM=file` writes the RAM for the log ring.
  - **Every UI page walked (2026-10-14)** with `tools/ui_walk.py`: one key script per
    main-menu page plus the sub-pages (route options, inputs, the confirm dialog, host
    scan, presets, input test), encoders, knobs, note keys, mapped buttons, GLO held,
    MIDI into the jack, the confirm dialogs answered both ways, a settings reset. 37
    runs, no halt: no decode gap left on the paths the UI reaches (the halt names the
    class and the pc; the image-wide instruction check cannot replace this, it loses
    alignment on this image). Rerun it after UI changes; its pictures
    (`build/ui_walk/walk.png`) show whether each script still lands where it should.
    Found by the pictures: the SDK's printf does not know the `+` flag, `%+d` printed a
    bare `d` (transpose, octave, input test, bend in the log); fixed, signs spelt out.
  - The `xTaskSwitchContext = 0` line seen in the device log (every 50 ms, 2026-10-14
    morning): it is `vPortYield` in the closed `cpu/wl82/liba/system.a` (port.c.o),
    `if (xTaskSwitchContext() == 0) puts("xTaskSwitchContext = 0\n")`, unconditional, no
    build option, printed when a yield finds no task to switch to (callers: the tick ISR,
    vTaskDelay, the queue and semaphore operations). Not reproducible since: the log ring
    and a breakpoint on that `puts` call stay empty with the committed firmware
    (fd0945c), with its predecessor (1784864, the SDK uart device still in), with the
    minimal build, in fm1-live and in the batch tool, with the plan's own key session,
    and in the emulator as it was before its slice fix (b7fa98a). Most likely a transient
    state of that morning's loop (an uncommitted variant of the bridge wait). If it shows
    up on hardware: `--break-pc` on the `call puts` inside `vPortYield` (`nm`, then
    `objdump -d`) with `FM1_BREAK_PCS=<vPortYield>` names the caller through `rets`;
    silencing it would be a filter in `log_putbyte` (saves the log space, not the mutex)
    or a post-link patch of that call; neither is in.
  - The stock VID/PID and the 0x13e00 block are still unmodelled (harmless).

## Questions to answer on the SDK tree (original list)

- Where is the update framework (`update`, `update_loader_download`, `UPDATA_PARM`)
  and does it accept a custom read/write transport? Which `.a` provides it?
- Does the SDK's USB device stack have a MIDI class, or do we add descriptors to a
  custom composite device? (`apps/common/usb/device/` or similar)
- BLE: GATT server API used by `demo_ble` (`le_gatt_common`, `ble_user_cmd`,
  `app_le_profile_config`), how to set a 128-bit service, notify, request a
  connection interval, bonding.
- Board files: which `board_ac791n_*.h` is closest to the FM-1 (UART1 on PH8, no
  SD, LCD on SPI1, SARADC, I2S codec), and how `isd_config.ini` sets the flash map
  so the app area stays within `[0x4000, 0x93000)` and the loader staging area is
  not clobbered.
- Toolchain: the SDK Makefiles expect `clang` for pi32v2 at a fixed path; map to
  `~/.jieli/toolchain`.
- Output: how `download.sh` / `isd_download` produce `jl_isd.fw` / `update.ufw`, and
  which pieces Felucca's `fm1pkg_make.py` re-creates. Our packager should wrap the
  SDK's `app.bin` the same way.
- Memory budget: the demo_ble footprint with BT on, versus the 0x8F000-byte app slot.

## Things to verify once an FM-1 is here

- Stock BLE name and whether stock already bridges TRS -> BLE (if it does, part of
  this project is moot and the value is USB <-> BLE and host mode).
- Chip key: read the identity, compare with Felucca's assumed 0x980F. **Answered:** 0x980F on
  the first unit (`hardware.md`).
- The 26-GPIO bond-out (no free pin, PC3 absent): `hardware.md`, "Adding a MIDI OUT".
  The MIDI OUT mod itself is not pursued (decided 2026-10-14: too invasive); the section
  stays as the record of what it would take.
- TRS jack wiring (Type A vs Type B) for the adapter cable. **Answered:** Type A (`hardware.md`).
