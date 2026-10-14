# Changelog

## Unreleased

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
  (`make MINIMAL=1`, identity FM-1_9B0), fuzz harnesses under ASan/UBSan,
  recovery script and flash dump/restore wrapper, `docs/recovery.md`,
  USB strings M-VAVE / FM-1.
- Footprint: 344 KB of the 581 KB app slot, 88 KB static RAM (minimal: 200 KB, 79 KB).
