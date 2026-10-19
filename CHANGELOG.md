# Changelog

## Unreleased

- BLE out: a packet the stack refuses (buffer full) waits and is retried every pass while the
  next one fills behind it, so batching adapts to the link; only a packet refused for 250 ms
  is dropped, counted in the Status page's dropped figure (`bridge_ble_drops`). Before, the
  refused packet was lost with its notes. Why: TRS to BLE worked on the first unit only with
  the sending device's MIDI clock off (2026-10-19). **Future work, verify on hardware:** TRS to
  BLE with the clock on; if notes still suffer, batch clock-only packets to the connection
  interval, or default the TRS to BLE route's clock option off.
- Screen-off is the panel's sleep (DISPOFF/SLPIN; SLPOUT, 120 ms, DISPON to wake), PA2 is left
  low: PA2 is also the key matrix 595s' /OE, so with the "backlight" off no button, key or knob
  read and nothing could wake the screen (found on the first unit 2026-10-19, as Felucca had on
  1.1.5). The press that wakes the panel is swallowed whole (down, hold and release). Verified:
  keys, buttons and knobs on all eleven columns, BLE MIDI from a phone through to USB, the wake.
- Editor command 0x0C (matrix): scan ticks, port levels, closed rows per column, a test word
  on the shift registers (all LEDs lit under it), the UI's event count and screen flag;
  `fm1_editor.py matrix` / `matrix_all`. The log's last 400 bytes are saved to the VM before
  a bridge-stall reset and replayed into the next boot's log (`FM1_STALLLOG_ID`).
- Open: twice today the unit stayed enumerated on USB but answered no SysEx until a power cycle
  (once with a bridge stall recorded in the health counters, once without); the saved stall
  log is there to catch the next one. The USB MIDI switch (USB page) stops the device and is
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
  written, a running `FM-1_9B1` would not have parsed as an FM-1 and the loader's
  `ota-FM-1_9B1` could not have been resumed. The identity is in `app/update/fm1_update.h`
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
