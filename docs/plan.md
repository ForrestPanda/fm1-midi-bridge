# Plan

## Milestones

0. **Research on the SDK tree (no hardware needed).** Answer the questions below and
   write them into the docs.
1. **Build an SDK app for the FM-1 board.** Clone `apps/demo/demo_ble` into `app/`,
   write a board file with the FM-1 pins, build to `app.bin`, package to a `.fwsc`
   the Felucca installer accepts. Nothing flashed yet.
2. **First boot (hardware).** Blink an LED / draw on the LCD from the SDK, confirm
   the update entry works *before* anything else: install, then install again,
   then install stock. Only then continue.
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

## Built so far (all host-tested where possible, nothing run on hardware)

- `app/bt_ble/ble_midi.c`: the SDK GATT server glue serving the BLE MIDI service
  (`midi_profile.h`), advertising the 128-bit service UUID, name "FM-1 MIDI",
  bonding on. API: `ble_midi_send`, `ble_midi_set_rx`, `ble_midi_ready`.
- `app/usb/usb_midi.c`: USB-MIDI class on the SDK USB stack, started on plug-in by
  the SDK's OTG detection (`task_pc.c`, `USB_PC_NO_APP_MODE`).
- `app/bridge.c`: the bridge task. UART1 on PH8 (SDK uart device "uart1", 31250)
  -> parser; USB and BLE input queued from their contexts; router fan-out; BLE
  packets batched per burst; update-protocol SysEx intercepted and handed to
  `fm1_update`.
- `app/update/fm1_update.c` + `fm1_update_port.c`: update step 1 (handshake
  identity, upgrade command, package reads, loader staging at 0xE0000, host
  confirmation, UPDATA_PARM at 0xE4F00 + RAM, reset) and the soft key to the
  mask-ROM USB mode. `tests/test_update.c` runs a whole session against the real
  `build/fm1bridge.fwsc` with a simulated installer and flash.
- `app/ui/lcd.c` + `midilog.c`: the panel (SDK SPI device "spi1" on port C, D/C on
  PC8 as GPIO, backlight PA2) and a logger screen: status line (USB, BLE, enabled
  routes) and the last 16 messages from any port, drawn at most 20 times a second
  from the bridge task. Every message the router sees is logged (first SysEx chunk
  only). The 8x14 font is generated from DejaVu Sans Mono.
- `app.bin` 310 KB of 581 KB; `tools/package.sh` -> `build/fm1bridge.fwsc`.

## UI (milestone 6, built 2026-10-11, untested on hardware)

- `app/input/`: own matrix scanner. TIMER3 at 10 kHz, one column per tick, rows read
  the tick after latching; debounce 8 frames; encoders with a continuously learned
  rest state; 32-event queue; button LEDs. `tests/test_matrix.c`.
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

### Verify on hardware (UI)
- Splash then main menu; SELECT scrolls; OCT+/OCT- navigate; HOME returns to the menu.
- The 10 kHz TIMER3 ISR does not disturb TRS MIDI, USB or BLE (log page still shows a
  TRS keyboard while scrolling menus). If TIMER3 is taken, use TIMER2 (`matrix_port.c`).
- Matrix tables (`app/input/matrix.c`): System > Input test shows every closed cell as
  (column,row), the decoded button, the encoder counts and learned rest states. Press
  every button and turn every encoder; fix the tables for any mismatch (batches differ).
- Debug console: the UART2 TX pin is -1 (nothing driven) until PC3 is confirmed free.
- Routes and filters survive a reboot; backlight times out and any key wakes it.
- Panic silences a stuck note on USB and BLE.
- USB page follows plug/unplug; "USB MIDI off" stops enumeration.
- Forget bonds forces a phone to re-pair.
- Host mode: a BLE MIDI keyboard appears in Scan, connects, its notes show as
  "BLE 90 .." and reach USB; host mode off makes the FM-1 advertise again. Risks: the
  server->client role switch at runtime, the `user_client_report_*` hook names, the
  `adv_report_t` packet offset, keyboards that insist on pairing ("Pair on connect").
- PC8 reclaimed as D/C after the SPI driver opened port C; panel init at 12 MHz.

## First-boot checklist (when an FM-1 is here)

No stock round trip is needed between our own installs: our package carries
Felucca's loader, which writes the app area (including the region descriptors) and
never the head. Stock v15 is the recovery path, not a prerequisite.

1. `tools/install.sh --info` on stock: note the identity and that the handshake works.
2. Back up: keep the stock `FM-1.fwsc`; confirm M-UPGRADE can reinstall it once.
3. Install `fm1bridge.fwsc`. Expected: the installer's step 1 is served by our app
   (17 read requests), the loader appears as `ota-FM-1_015`, step 2 writes.
4. On boot: does it enumerate on USB as a MIDI device? Does the BLE name appear?
   Does `tools/install.sh --info` get our identity `FM-1_9B1`?
5. Install again (proves our step 1 on hardware), then install stock back.
6. Only then: TRS keyboard -> BLE/USB, USB <-> BLE, soft key, debug pin PC3.
   The logger screen shows every message as it passes, so a keyboard on TRS should
   print "TRS 90 3C 64" lines without any host attached. If the panel stays dark:
   check that the SDK SPI driver left PC8 usable as the D/C output (we reclaim it
   after `dev_open("spi1")`), and the 12 MHz clock.

## Still open

- USB descriptor still says JieLi VID 0x3654 and the SDK product string.

- Whether `update.a` accepts a Felucca-style `.fwsc` in step 1 (it looks for
  `flash.bin` and `ota.bin` inside a UFW and checks chip name, uboot and key): test on
  hardware, or write our own step 1 (the protocol is small).
- Step 1 of the update inside our app (handshake identity, upgrade command, 0x30
  reads, staging the loader, `UPDATA_PARM`): vendor `update.a` over a SysEx transport
  shim, or our own ~300 lines. Must exist before the first install.
- Stock VID/PID and product string; our descriptor currently says JieLi 0x3654.
- The pin-level board file for the FM-1 (LCD, keys, LEDs, codec) in SDK terms.
- Debug output: the demo prints on UART2 TX = PC3. Unknown what PC3 is on the FM-1;
  switch to USB CDC or confirm the pin is free before the first boot.

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
- Chip key: read the identity, compare with Felucca's assumed 0x980F.
- Whether UART1 TX is routed anywhere near the TRS jack (MIDI OUT mod).
- TRS jack wiring (Type A vs Type B) for the adapter cable.
