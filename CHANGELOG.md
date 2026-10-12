# Changelog

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
