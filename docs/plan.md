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
  (`make MINIMAL=1`, `app/minimal.c`, identity FM-1_9B0, `tools/package.sh --minimal`);
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

### Verify on hardware (UI)
- Splash then main menu; SELECT scrolls, clockwise = down (if it is up, the encoder
  sign in `matrix.c` `qtab` is the other way round on real parts); OCT+/OCT- navigate;
  HOME returns to the menu.
- The 10 kHz TIMER3 ISR does not disturb TRS MIDI, USB or BLE (log page still shows a
  TRS keyboard while scrolling menus). If TIMER3 is taken, use TIMER2 (`matrix_port.c`).
- TRS MIDI IN through `app/trs_uart.c` (register level, Felucca's set-up): a keyboard on
  the jack logs on the MIDI log page and reaches USB/BLE within a few ms with the FM-1's
  own keys idle; with nothing on the jack, no UART interrupts (Statistics page).
- The FM-1's own keys reach USB/BLE with nothing plugged into TRS (the bridge loop used
  to wait for TRS bytes; it now sleeps on a semaphore every producer posts).
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
- The SDK log hook: does the vendor library call `log_putbyte()` per byte, so SDK
  printf output lands in the device log? Our own `fm1_log()` lines do regardless.
- Stack margins in the Device card after an hour of traffic (bridge and UI tasks).
- USB strings: does M-UPGRADE still find the device as "FM-1"? Stock VID/PID.
- `tools/fm1_recover.py`: the soft key on stock and on ours, the vendor request on
  ours; the UBOOT disk's SCSI vendor/model strings; jl-uboot-tool on WL82 (listed
  as untested there) and the loader it needs.

## First-boot checklist (when an FM-1 is here)

No stock round trip is needed between our own installs: our package carries
Felucca's loader, which writes the app area (including the region descriptors) and
never the head. Stock v15 is the recovery path, not a prerequisite.

0. Build the D+/D- dongle (ip2k's `dongle/`), clone jl-uboot-tool, and take a full
   1 MB dump of the untouched unit: `tools/fm1_recover.py`, `tools/fm1_flash.sh dump`.
   The dump is the only backup of the unit's own data (BT address, BTIF). If the soft
   key does not get stock into UBOOT mode, the dongle does. Read `docs/recovery.md`.
1. `tools/install.sh --info` on stock: note the identity and that the handshake works.
   Note the USB VID/PID and product string of stock (`lsusb -v`) for `app/usb/descriptor.c`.
2. Back up: keep the stock `FM-1.fwsc`; confirm M-UPGRADE can reinstall it once.
3. Install `fm1bridge-minimal.fwsc` first (identity FM-1_9B0: no radio, no keys, no UI
   task). Expected: the installer's step 1 is served by our app (17 read requests),
   the loader appears as `ota-FM-1_015`, step 2 writes.
4. On boot: the status screen; USB enumerates as "FM-1" MIDI; `tools/install.sh --info`
   gets `FM-1_9B0`; the web editor connects, the Device log card shows the boot lines
   (and whether SDK printf output reaches the ring through `log_putbyte`), the Device
   card shows heap and stack margins; a TRS keyboard counts on the screen.
5. Install the minimal build again (proves our step 1 on hardware), then install
   stock back, then the full `fm1bridge.fwsc` (identity FM-1_9B1).
6. Only then: TRS keyboard -> BLE/USB, USB <-> BLE, soft key (`tools/fm1_recover.py
   --softkey`, then the vendor request). PC3 stays undriven; the SysEx log is the console.
   The logger screen shows every message as it passes, so a keyboard on TRS should
   print "TRS 90 3C 64" lines without any host attached. If the panel stays dark:
   check that the SDK SPI driver left PC8 usable as the D/C output (we reclaim it
   after `dev_open("spi1")`), and the 12 MHz clock.

## Still open

- Stock USB VID/PID: `app/usb/descriptor.c` says M-VAVE / FM-1 but keeps JieLi's
  0x3654 VID and the SDK PID until read from a real unit (`lsusb -v` on stock).
- Whether the hardware-side details of the board file are right: the LCD SPI pins,
  the key matrix tables (may differ by batch), the codec lines, PB1 never driven.
- Debug UART: the SDK demo prints on UART2 TX = PC3. Every open FM-1 firmware
  leaves PC3 alone (Felucca drives PC0/1/2/6 for the codec and PC7..PC10 for the
  panel; nothing documents PC3, PC4, PC5) and there is no schematic, so "free" cannot
  be shown from the trees. The board file keeps `tx_pin = -1`; the device log over
  SysEx is the console. Only worth revisiting if a boot never reaches USB.
- Whether `log_putbyte` receives the vendor libraries' printf output (hardware).
- jl-uboot-tool on the WL82 family (listed untested upstream), the soft key and the
  SDK vendor request on stock: `docs/recovery.md`.
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
- Chip key: read the identity, compare with Felucca's assumed 0x980F.
- Whether UART1 TX is routed anywhere near the TRS jack (MIDI OUT mod).
- TRS jack wiring (Type A vs Type B) for the adapter cable.
