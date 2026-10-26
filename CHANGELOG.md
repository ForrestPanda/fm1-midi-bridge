# Changelog

## Unreleased

- Licensing, after an audit of the tree against the licences it claims (2026-10-25):
  - `LICENSES/` holds the full texts the Apache-2.0, GPL-3 and Bitstream Vera licences require
    to travel with the code and the binaries (plus TinyUSB's and micro-ecc's notices); the
    tree had only links before. `THIRD-PARTY-NOTICES.md` lists what the release packages
    carry from others: the SDK's libraries and the third-party code known inside them
    (micro-ecc, newlib, FreeRTOS, compiler-rt), the SDK's three boot files, Felucca's loader.
  - `tools/release.sh` assembles the release folder: both packages, `SHA256SUMS`,
    `licenses.zip` and `felucca-v1.6-source.tar.gz`, the complete source of the update
    loader inside each package (GPL-3 section 6 asks the distributor to keep it available,
    not only to point at Felucca's repository). It checks that the loader in the packages is
    the one built from the clean Felucca tag.
  - The font headers carry the Bitstream Vera copyright and permission notice, as that
    licence requires of every copy (`tools/mkfont.py` emits it; the glyphs are unchanged).
  - `app/bt_ble/ble_host.c` is Apache-2.0 with JieLi's notice kept, like `ble_midi.c`: it
    is derived from the SDK's `le_net_central.c`, the same relationship that already made
    `usb_midi.c` Apache-2.0. `tools/fm1pkg_make.py` names its modification date (GPL-3
    section 5a). `tools/fm1_install_paced.py` says that it runs as a GPL combination with
    Felucca's installer. `tools/trs_tester/usb_descriptors.c` keeps TinyUSB's MIT notice,
    being adapted from its `midi_test` example.
  - Nothing in the firmware changed; the packages of v0.2.0 are unchanged.

## v0.2.0 (2026-10-24): first public alpha

Packages: `fm1bridge.fwsc` (`FM-1_931`) and `fm1bridge-minimal.fwsc` (`FM-1_930`), with
Felucca v1.6's update loader. Install: `docs/install.md`.

- Pairable off (Bluetooth page) now keeps the FM-1 hidden after a disconnect too; it used to
  advertise again at the next disconnect while the row still read "off"; leaving host mode
  follows it too. Still not saved: a reboot makes it findable again. Verified on the unit
  (2026-10-24): hidden after a disconnect and out of range, back on when switched on, hidden
  after host mode, advertising after a reboot.
- Verified on the unit (2026-10-24): this release installs over another firmware (BackBeat 0.9,
  `FM-1_985`) and installs BackBeat's release package over itself, both through Felucca's
  installer.
- The Clock master page's hint and the web editor say how to start the clock: OK on the Clock
  row, or PLAY once PLAY has no MIDI job (it sends CC 64 out of the box).
- Version 0.2.0 (splash, System page, editor INFO).
- Tools: `fm1_flash.sh restore-app DUMP` writes only the app area of a dump back (the recovery
  that worked on the first unit, by hand then); `fm1_recover.py` takes only the JieLi ROM disk
  (USB 4c4a:8057, or a WL8x UBOOT model), never another USB disk; `install.sh` takes a package
  (`install.sh FM-1.fwsc` goes back to stock); the scripts find Felucca and jl-uboot-tool next
  to the clone or one level up; `fm1_safe_install.sh` uses Felucca's installer by default.
- Docs: `docs/install.md`; the manual's Connecting section, the way back to stock as verified
  (Felucca's installer; M-UPGRADE untested), corrected PLAY, Recorder, clock jitter and Pairable
  text; recovery's ladder with `restore-app` and the loader's real identity (`ota-FM-1_900`);
  the editor protocol table complete; screenshots regenerated from the current pages.

Earlier, since v0.1.0 (in the order written; a later entry supersedes an earlier "verify"):

- Screen off turns the backlight off too, not just the panel: the matrix scan holds PA2 (backlight
  and the 595s' /OE) low only for the few microseconds of each read, so the backlight and the button
  LEDs are dark but for a faint glow, and every press still reads. `MX_DARK_SETTLE_US` (4) is the
  rows' settle. Verified by the user on the unit (2026-10-24): the backlight goes dark and the
  panel wakes again.
- Preset names: up to 12 characters of plain ASCII per slot, set in the web editor's Presets card
  (or `fm1_editor.py names SLOT "NAME"`), shown on the FM-1's Presets page in place of the summary
  and on a slot's own page. Their own flash record (id 16), so the settings layout and the presets
  stay as they were; a save keeps a slot's name, a clear drops it. Editor command 0x0E. Host test;
  set and read back on the unit from the command line; the web page and the panel verified by the user.
- Composer on the unit (2026-10-21): every kind sent to USB arrived with the right bytes (note on and
  its release 300 ms later, CC, program, bend, both aftertouches, clock, start, continue, stop, the
  device inquiry); a favourite stored a program change and sent it back as stored.
- Recorder on the unit (2026-10-21): an 8-note phrase with uneven gaps sent into USB, recorded from
  the page's Record row and played back to USB: all 17 events, every note and velocity, starts
  within 6 ms of the original and lengths within 10 ms (the 10 ms tick).
- Battery: PB1 was read without being set up as an analog input or sampled, so the bar was always
  empty and the voltage 0.00 V. Now set up as the MASTER pot is; the bar uses Felucca's and SLOOP's
  raw thresholds (the only figures for this board). On USB: raw 619, 2042 mV at the pin, so a 1:2
  divider and a 4.07 V cell, fully charged: the fourth cell from 611, so a full charge shows four.
  The screens show only the bar (the user's call), the System page the cell voltage and the raw
  value. Not checked against a meter.
- TRS in while the menus scroll: three runs of the TRS suite, 30 of 30, the user scrolling menus,
  the log and Statistics, no lag. The panel wakes on music keys (the user).
- Host mode, a stale bond, now seen working (2026-10-20 night, the PC as a pairing keyboard, its
  bond removed while not connected): the PC answered our stored keys with 06, the FM-1 forgot the
  bond, reconnected and paired fresh (PAIR_PROCESS 10), subscribed 1.7 s after the refusal. Two
  bugs on the way, both in the fix of the same day: the bond list keys a device by its address in
  the other byte order (the delete found nothing; check and delete now try both orders), and the
  reconnect cleared `peer` before copying the address out of it (it looked for 00:00:00:00:00:00).
  Also handled now, **not yet seen**: a peer that hangs up (13 or 05) within 2 s at encryption, twice
  in a row, while we hold a bond, is treated like the 06 (BlueZ did that once, on the build before).
  BlueZ told to forget a device that is still connected keeps its keys: test with the link down.
- Host mode, Pair on connect and a keyboard that does not pair: the Casio WU-BT10 adapter never answers a
  pairing request, and the FM-1 waited for the encryption and dropped the link 15 s later, on every
  attempt. Now discovery starts anyway after 4 s ("no pairing in 4 s, going on without"); verified
  with the WU-BT10, notes both ways. Its register cleared, the WU-BT10 worked again in host mode
  (74 messages keyboard to USB, notes back heard) once the tablet's Casio app, which connects to it
  on its own, was off.
- The user's manual checks on the first unit, 2026-10-20 evening, with the PC listening:
  *Panic* sent 2080 messages to USB and to BLE alike (CC 120 and CC 123 on 16 channels, all
  2048 note offs); the Statistics page counted a known 10 s pattern exactly (every kind, note
  and velocity range, 120 bpm, channels 1, 3, 5, 10); menus, the log scrolled back, the hold,
  the filters and Statistics under 2 minutes of clock and notes: no lag, no glitch; *USB MIDI*
  off drops the device from the PC within a second, on re-enumerates it; *Forget bonds* went
  from 2 to 0, the PC's old keys then failed (the link dropped within 10 s, as with any BLE
  device that forgot a bond) and once the PC forgot too, 300 of 300 each way; the soft key
  `F0 22 24 35 7D F7` drops this firmware into the ROM (4c4a:8057) within seconds, and switch off,
  unplug, switch on brings it back with its settings (switching off alone does not: the cable
  powers the chip); the web editor's Backup card (Export, change, Import, Write) restored the
  settings, and its Firmware card reinstalled `FM-1_931` ("done: FM-1_931", settings kept,
  traffic after it 300 of 300).
- The phone's own view of PC to phone (POCO F3, android-midisuite's BLE pairing app and
  MidiScope, the FM-1 connected as a system MIDI device): 12 notes with their velocities, 13
  CC 74 values, a program change and two pitch bends sent into USB arrived in order, every value
  right (MidiScope shows a bend as its two data bytes: 0 96, then 0 64).
- Web editor: Connect took the first port whose name held "midi", so "Midi Through" on Linux,
  and the Firmware card re-found the device the same way. Now a port list next to Connect,
  the FM-1 (or its loader) first and Midi Through or a tester never by default; choosing
  another port reconnects. Verified in Chrome on the bench.
- Pairing asked for by the central, after the change below (no security request, Just Works
  without MITM), on the first unit 2026-10-20: the PC (`bluetoothctl pair`, a NoInputNoOutput
  agent) paired, the FM-1 stored the bond (PAIR_PROCESS 10); every reconnect encrypted with the
  stored keys (LL_ENC_REQ, PAIR_PROCESS 01, encryption change 0), no pairing again; 300 of 300
  each way over the encrypted link. An unpaired phone app stays connected as before.
- Verified by the user: encoder direction and acceleration, the holds, the backlight timeout and
  wake-up press, the Perform page's font. The USB-silent episodes of 2026-10-19 did not come back
  in a whole day of testing: closed.
- MIDI log filters: on the MIDI log page FX, SCL, ENV and LFO show or hide notes, program
  changes, control changes and everything else, each button lit while its kind is listed;
  the status line shows "show N P C O" with a "-" per hidden kind. Only the listing changes,
  not what the bridge forwards. Kept in the settings (`log_hide`, a former pad byte at 208:
  no new version); `fm1_editor.py set log_hide=N`, the web editor knows the offset. On that
  page the four buttons are filters even when they have a MIDI job. Verified on the unit.
- Peripheral mode no longer asks every central to pair (the SDK demo's security request, with
  MITM asked of a device with no display and no keys). An Android phone (POCO F3) whose MIDI app
  connected and subscribed but never paired had the pairing failed by our side after ~12 s and
  hung up, on every connection (2026-10-20). Now Just Works bonding when the central asks for
  it, nothing otherwise: the phone stays connected; PC to phone 3624 messages with no drop
  (scale, a burst of 600, 30 s at 100 messages/s), phone to PC 317 messages, every one. The
  phone set the link to 11.25 or 37.5 ms after our 7.5 ms request.
- Host mode on today's firmware with the PC as the keyboard: keyboard to USB every note; USB to
  the keyboard 1002 of 1002 at 100 messages/s and a burst of 600 whole in 29 packets (the BLE
  queue in host mode). Pair on connect: paired and encrypted.
- Host mode, a stale bond: the FM-1 asked for encryption with keys the PC no longer had (status
  06), the discovery started at connect never finished, and the link was dropped after 15 s,
  every attempt. Now that peer's bond is deleted and the FM-1 connects to it again
  (`unbond_retry`). (Seen working 2026-10-20 night, the entry above.)
- `tools/ble_midi_peripheral.py`: a PropertiesChanged without the invalidated list no longer
  throws in the handler.
- TRS MIDI IN tested on the first unit (2026-10-20) with `tools/trs_tester/`, an STM32F4-Discovery
  that plays byte scripts on the TRS line with microsecond timing, and `tools/trs_test.py`, which
  works out what the FM-1 should forward (a port of its parser and merge guard) and checks USB
  (the raw ALSA device) and BLE (the PC as the central). All ten tests pass on both: notes,
  clock at 120 and 300 bpm under notes (the open TRS to BLE with a clock item: no loss, BLE
  latency median 5.8 ms), the line saturated for 3 s, running status, real time inside messages
  and SysEx, 1 KB and 4 KB SysEx, garbage (orphan data, undefined statuses, cut-off messages),
  the baud rate off by up to 4 %, and line breaks. TRS to USB latency from the last byte on the
  line: median 0.8 ms. Three fixes on the way:
  - `app/trs_uart.c`: every byte was handed over one byte late, so a message's last byte waited
    for the next message (forever for the last one: a held note-off). The ISR read HRXCNT in the
    same instant it set the latch, and got the previous latch's count. Now JieLi's sequence:
    latch, clear the timeout pending, clear the RX pending, a few nops, read.
  - A line break (a cable going in or out under running status) arrived as a 00 data byte and
    completed a note (90 00 28): this UART flags no framing error. The timeout interrupt, 1 ms
    after the last byte, now drops a trailing 00 when the line (PH8) is still low; a break under
    1 ms still gets through. Counts in the log (`trs: rx irq, timeout irq, bytes, breaks`).
  - USB out: a real-time byte inside a SysEx makes the parser hand over the piece so far, and
    `usbmidi_encode()` dropped the 1 or 2 bytes over a multiple of 3 (F0 7D 01 02 F8 03 04 FA 05
    F7 reached USB as F0 7D 01 05 F7). They wait for the next piece now (`usb_emit`). Host test
    `test_usb_sysex_realtime`.
- `ble_midi.c` printed a log line for every BLE write (the SDK demo's), which flushed the device
  log within a second under a phone or keyboard: gone.
- `tools/usb_to_ble.py --clock BPM` (a MIDI clock under the notes; spacing on arrival and in the
  FM-1's timestamps) and `--reverse` (BLE to USB). On the unit: USB to BLE with a clock at 120,
  200 and 300 bpm under notes, no loss, latency median 5-7 ms; BLE to USB steady, bursts of 300
  and a 200 bpm clock, no loss. Settings, routes and presets survive a reboot; a reinstall of the
  same image went through (the loader drop of 2026-10-19 not seen again).
- Soak, 2026-10-20: 55 minutes, about 270 BLE connections both ways with a clock and bursts: the
  FM-1's heap, stack margins and hang counters did not move. After ~45 minutes the PC's AX200 /
  BlueZ slowed the FM-1 to PC direction to under 1 kB/s on every connection (and aborted a
  connect now and then) until the adapter was powered off and on: on the PC, not the FM-1.
- USB to BLE verified on the first unit (2026-10-20, `tools/usb_to_ble.py`: the PC as the BLE
  central over BlueZ, notes and CCs into the USB port, every message checked): steady rates up
  to 600 messages/s arrive whole and in order, latency median 7 ms. Bursts did not: 300
  messages at once lost a fifth, 600 lost two thirds, while the link carried 197-byte packets
  at 12 kB/s. The bridge held one refused packet and the encoder's, and dropped the older when
  the encoder filled. Now a 4 KB FIFO of packets waits behind the refused one (`ble_q`,
  `app/bridge.c`); a packet older than 250 ms when its turn comes, or the oldest when the queue
  is full, is dropped and counted, so latency stays bounded. On the unit: 300 and 600 at once
  arrive whole (latency max 77 and 192 ms); 1200 at once lose 2%, on the USB side. Host test
  `test_ble_burst`; the bridge fuzzer now turns a busy stack on and off.
- The WU-BT10 keyboard in host mode sounds the notes the FM-1 sends (2026-10-20).
- BLE host mode verified on the first unit (2026-10-19) against a BLE MIDI peripheral made of
  the PC's Bluetooth adapter (`tools/ble_midi_peripheral.py`, BlueZ over D-Bus: advertises
  the MIDI service, plays a note every so often once subscribed, prints what the central
  writes, answers pairing requests): scan, connect, discovery, subscription, notes both ways,
  reconnect after a drop, pairing on connect, host mode off and on at runtime; then a real
  keyboard through a Casio WU-BT10 adapter (no name, no GAP name: the list shows its address;
  notes with velocities to USB, notes from the FM-1 out to it). Fixed on the
  way: the stack reports 128-bit UUIDs big-endian (the characteristic never matched); a
  reconnect was a blind create connection and never completed (now the scan looks for the
  remembered address and connects on sight, the SDK's way; a stuck attempt is cancelled after
  10 s); `ble_op_conn_param_request` is the slave's L2CAP request and reset the chip when the
  central used it (`ble_op_conn_param_update` now); the peripheral's parameter request is
  refused unless it asks for 15 ms or less or the link profile is power saving; entering and
  leaving host mode wait for the old link's disconnect event and rest 200 ms before the role
  changes; with "Pair on connect" discovery waits for the encryption; a device whose MIDI
  UUID is only in its scan response is listed too; the remembered peer's name is read from
  its GAP name when the scan gave none; a boot that never ran clean does not enter a stored
  host mode (`health.unclean`); the panel's Disconnect acts on the host link in host mode.
- A USB bulk write blocked the bridge loop for good when no program on the PC read the MIDI
  port (Linux polls bulk IN only with a reader open): two stall resets on a link-drop panic
  burst. `usb_midi_write` checks the IN FIFO first, waits up to 50 ms (yielding) for a host
  that is reading, and once a packet has been left behind drops at once until the FIFO is
  found empty (`bridge_usb_drops`, in the Status page's dropped figure). A 3 ms bound lost
  editor replies on the first unit.
- A settings write from the editor (0x03) applied the panel's invert and the keyboard reload
  from the bridge task; the UI task does it now at its next tick (`ui_settings_changed`).
  `usb_port_apply` only starts or stops USB on a change.
- The SDK log hook: the vendor library does print through `log_putbyte()` (the first unit:
  LL, SM, VM, USB and a task and heap table every minute). That table is filtered out of the
  device log (`app/fm1_log.c`), it would have pushed everything else out within three minutes.
- Editor command 0x0D (host): the host mode's scan list and the Scan page's buttons (connect
  by index, disconnect, scan on/off) over SysEx; `fm1_editor.py host`, `set FIELD=VALUE`
  (writes a setting: `set ble_mode=1` is host mode), `send PORT HEX ...`, `settings`.
- BLE out: a packet the stack refuses (buffer full) waits and is retried every pass while the
  next one fills behind it, so batching adapts to the link; only a packet refused for 250 ms
  is dropped, counted in the Status page's dropped figure (`bridge_ble_drops`). Before, the
  refused packet was lost with its notes. Why: TRS to BLE worked on the first unit only with
  the sending device's MIDI clock off (2026-10-19). (Verified 2026-10-20: TRS to BLE with a clock
  on, the TRS tester's suite, no loss.)
- Screen-off is the panel's sleep (DISPOFF/SLPIN; SLPOUT, 120 ms, DISPON to wake), PA2 is left
  low: PA2 is also the key matrix 595s' /OE, so with the "backlight" off no button, key or knob
  read and nothing could wake the screen (found on the first unit 2026-10-19, as Felucca had on
  1.1.5). The press that wakes the panel is swallowed whole (down, hold and release). Verified:
  keys, buttons and knobs on all eleven columns, BLE MIDI from a phone through to USB, the wake.
- Editor command 0x0C (matrix): scan ticks, port levels, closed rows per column, a test word
  on the shift registers (all LEDs lit under it), the UI's event count and screen flag;
  `fm1_editor.py matrix` / `matrix_all`. The log's last 400 bytes are saved to the VM before
  a bridge-stall reset and replayed into the next boot's log (`FM1_STALLLOG_ID`).
- Open then: twice today the unit stayed enumerated on USB but answered no SysEx until a power
  cycle (once with a bridge stall recorded in the health counters, once without); the saved
  stall log is there to catch the next one. (Not seen again after the USB write fixes of
  2026-10-19 evening and 2026-10-20.) The USB MIDI switch (USB page) stops the device and is
  saved: a unit that no longer shows on USB may simply have it off.
- First install on hardware (2026-10-19, the record in `docs/recovery.md`): minimal build,
  stock back, the full bridge; it boots, advertises "FM-1 MIDI". The panel did not react to the
  SDK's "spi1" device, so `app/ui/lcd.c` drives SPI1 at register level now (Felucca's values);
  its first version DMA'd a `const` byte out of flash and soft-locked the unit (a watchdog loop
  before USB): payloads now go out of RAM, waits are bounded, a dead panel is given up, and the
  UI leaves the panel alone after three unclean boots in a row (`health.unclean`,
  `HEALTH_SAFE_BOOTS`; the panel init announces its stages to the record, `health_stage`).
  Recovered through the mask ROM with `tools/dongle/stm32f4disco/` (the USB key from an
  STM32F4-Discovery, verified) and jl-uboot-tool's write of the stock app area (verified);
  `tools/dongle/fm1_key.py` is the RP2040 MicroPython version, untested. The final driver
  (Felucca's path alone) shows the minimal build's status lines and the full bridge's UI.
  Found there: the USB receive ring (512 bytes) lost the tail of the update protocol's
  512-byte read reply, sent by the host as one bulk burst while the loop slept a tick, so
  our own step 1 never saw a reply (nothing was written). `USB_RING` is 2048 now
  (`tests/test_bridge.c` checks the new bound). New tools: `fm1_install_paced.py` (the
  installer with the host's SysEx sent as sequencer events 48 bytes 2 ms apart: installs
  over a firmware with the old ring) and `fm1_editor.py` (info, status, sysinfo and the
  device log from the shell). `fm1_safe_install.sh` reported exit 0 on a failed install.
- Identities renamed `FM-1_931` (full) and `FM-1_930` (minimal), from `FM-1_9B1` / `FM-1_9B0`
  (2026-10-19, before the first install): Felucca's and the drummer's installers accept
  `FM-1_` followed by digits only, so the old packages were refused before anything was
  written, and a running `FM-1_9B1` would not have parsed as an FM-1. (The loader in our
  packages is Felucca's, which always answers `ota-FM-1_900`.) The identity is in `app/update/fm1_update.h`
  (`UPD_IDENTITY`), `app/Makefile` (the minimal build) and `tools/package.sh`.
- UI pass (2026-10-16), every page walked again in fm1-emu (`tools/ui_walk.py`, 59
  runs): the Status page (ports and routes) is the home page (HOME from anywhere, hold HOME for the
  new Perform page in a 16x28 font); the cursor is kept when coming back from a
  sub-page or a dialog and skips information rows; hold OCT+ is a row's second action
  (a route's options, a preset's page, storing a favourite, holding the log); KNOB1..4
  accelerate on quick turns; the confirm dialog starts on "no"; every title row is a
  blue band (the cursor stays the white bar) and shows the USB and Bluetooth state; the
  home page is a dashboard, not a list (big-font links, counter and route tables, a
  footer band; titled Status); pages that only refresh values redraw through the row
  cache instead of repainting; switches are green when on; low battery, crashes and
  input drops are orange; actions answer with a one-second note on the footer; the
  main menu is grouped (Setup, Bench, Device; Presets moved out of System); Routing is
  one row per route; the MIDI log keeps 64 lines with SELECT scrolling back and a
  hold; presets show what they hold; the battery as a bar (thresholds unverified).
  Fixed: the first press after the backlight went off was lost (a note or the PLAY
  page turn did nothing); the Keyboard page's rows could not be changed from the
  panel; SELECT was swallowed on the Keyboard page.
- Runs in fm1-emu (2026-10-14): boots, UI, keys, encoders, TRS in, routing to USB and
  BLE, the device log readable from RAM. Found there and fixed:
  - the bridge loop paced itself with a blocking UART read, so keyboard, composer, USB
    and BLE traffic only moved while TRS bytes arrived; it now sleeps on a semaphore
    that every producer (USB ISR, BT task, UI, TRS interrupt) posts;
  - TRS MIDI IN is read at register level (`app/trs_uart.c`, Felucca's UART1 set-up,
    RX and timeout interrupts) instead of through the SDK's uart device;
  - the encoders counted clockwise as -1 (`matrix.c` `qtab`, now Felucca's sign).
  The scan period and the ISR priority can be set on the make line (`MX_TICK_US`,
  `MX_IRQ_PRIO`). The emulator ran this firmware at 0.08x real time (its cpu1 slice
  ended inside the SDK's p33 critical section every pass, cpu0 spun on the lock); with
  cpu1's slice ending at `idle` it runs at 0.93x (`docs/plan.md`, fm1-emu item). The
  routing screen halted the emulator on a frame store it could not decode; fixed there.
- Every UI page walked in the emulator with key scripts (`tools/ui_walk.py`, 37 runs
  through fm1-emu's live tool, pictures in `build/ui_walk/`): no halt, so no decode gap
  on the paths the UI reaches. Found by the pictures and fixed: the SDK's printf does
  not know the `+` flag, so transpose, octave, the input test counts and the bend value
  in the MIDI log showed a bare `d`; the sign is spelt out now.
- The bridge task runs on the host: `tests/host/` simulator, `bridge_step()`,
  `tests/test_bridge.c` and `tests/fuzz/fuzz_bridge.c`.
- Fixed: a SysEx from USB that starts like one of ours (update, soft key, editor) but
  is longer than any accepted frame no longer leaks its tail chunks to the router.
- `tests/test_update.c` runs full -> minimal -> full against both device identities.
- Fuzz driver: `FUZZ_SEED`, crashing inputs saved for replay; `tests/fuzz/soak.sh`.
- Docs: the open-items list brought up to date; PC3 (SDK console pin) recorded as
  unused by every open firmware and left undriven; screenshot of the minimal build.

## v0.1.0 (2026-10-12): first hardware candidate

Everything is built and host-tested; nothing has run on an FM-1 yet.

- TRS MIDI in, USB MIDI device, BLE MIDI peripheral and (exclusive) host mode, a
  4x4 routing matrix with per-route channel filter, remap, clock and SysEx options,
  per-input transpose and velocity curves, filters, panic, panic on link loss.
- Panel UI: splash, main menu, routing, MIDI log (raw and decoded), composer,
  connections, statistics, recorder, reference, clock master, keyboard mode,
  button and key MIDI jobs, Bluetooth (link profiles, scan, bonds), USB, display,
  system (health, presets, input test).
- Settings v9 in syscfg with CRC, four presets, web editor over USB-MIDI SysEx with
  backup/restore and firmware install.
- Update step 1 compatible with M-VAVE's protocol, Felucca's loader, packager fork
  with the VM region kept clear of the staging area, soft key to the mask ROM.
- Hardware readiness: device log over SysEx, minimal first-install build
  (`make MINIMAL=1`, identity FM-1_930), fuzz harnesses under ASan/UBSan,
  recovery script and flash dump/restore wrapper, `docs/recovery.md`,
  USB strings M-VAVE / FM-1.
- Footprint: 344 KB of the 581 KB app slot, 88 KB static RAM (minimal: 200 KB, 79 KB).
