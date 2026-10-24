# Web editor

`web/editor.html` edits every setting from a browser over USB-MIDI, using Web MIDI
(Chrome or Edge). Web MIDI with SysEx needs a secure context: serve the page over https or
from localhost:

```
cd web && python3 -m http.server 8000     # then open http://localhost:8000/editor.html
```

Click Connect, allow SysEx, and the page reads the device info, the settings and the
counters. Change anything and click Write: the device checks the layout, applies the
settings at once (routes, filters, keyboard, BLE mode and link profile, USB, display)
and stores them a second later. The Send MIDI card sends any bytes you type, with quick links for a note, a volume change, a program change and a device inquiry, and the monitor decodes what comes back. The Key MIDI card gives any of the 27 keys a job instead of its note. The page also prints every other MIDI message that
arrives from the device, which makes it a second MIDI monitor.

## Protocol

`F0 7D 'F' 'M' '1' cmd [payload] F7`, manufacturer id 7D (non-commercial).

| cmd | reply | payload |
| --- | --- | --- |
| 0x01 info | 0x41 | ASCII `identity;version;settings_version;settings_size` |
| 0x02 get settings | 0x42 | pack7(`fm1_settings`) |
| 0x03 set settings, pack7(`fm1_settings`) | 0x43 | one status byte: 0 ok, 1 short, 2 bad magic or size, 3 wrong version |
| 0x04 status | 0x44 | pack7(`editor_status`): in/out counters per port, dropped, USB, BLE |
| 0x05 presets | 0x45 | one byte per slot, 1 = used |
| 0x06 / 0x07 / 0x08 + slot | 0x46 / 0x47 / 0x48 | save / load / clear preset slot 0..3; one status byte |
| 0x09 + port + pack7(bytes) | 0x49 | send raw MIDI bytes (at most 128, SysEx included; a longer message is refused) to port 1 = USB, 2 = BLE, 3 = both, bypassing routes |
| 0x0A + from (4 x 7 bits, LSB first; optional) | 0x4A | pack7(u32 total, u32 from, bytes): the device log from absolute offset `from`, at most 200 bytes; `total` is the count ever written, `from` the offset actually served (clamped to the 4 KB the device still holds). Repeat with `from + bytes` until `from` reaches `total` |
| 0x0B sysinfo | 0x4B | pack7(`editor_sysinfo`): uptime ms, free heap, bridge and UI task stack margins (words never used), boots, watchdog resets, software resets, bridge stalls, last reset reason, minimal-build flag |
| 0x0C [word (2 x 7 bits) ticks (2 x 7 bits)] | 0x4C | pack7(u32 scan ticks, u32 port A in, u32 port B in, 11 x u8 closed rows per column, u8 rows read under the test word, u16 port A DIR, u16 port A OUT): the key matrix as the scan interrupt sees it (zeros on the minimal build). With a payload, that word goes on the shift registers for that many scan ticks (a test). `tools/fm1_editor.py matrix` |
| 0x0D + op + arg (both optional) | 0x4D | pack7(u8 host state, u8 count, count x {u8 addr type, u8 addr[6], s8 rssi, char name[20]}): the BLE host mode's scan list in the Scan page's order. op 1 + index connects to that device, op 2 disconnects, op 3 + 0/1 stops or starts the scan; the list comes back either way. `tools/fm1_editor.py host` |
| 0x0E [slot name...] | 0x4E | one status byte (0 ok, 1 bad slot, 2 write failed) + 4 x 12 bytes: the preset names, ASCII, 0-padded. With a payload: the slot, then up to 12 bytes of printable ASCII (none clears the name). `tools/fm1_editor.py names` |

pack7 is the same 8-to-7, LSB-first packing the M-VAVE update protocol uses. The
settings blob is `fm1_settings` from `app/settings.h` byte for byte; the page carries the
offsets of the current version and `tests/test_editor.c` asserts them, so a layout
change fails the host tests until both are updated. The device keeps its own
`host_peer` field (the last BLE host-mode device) and recomputes the CRC on write.

## Device log

The FM-1 has no debug pads, so the firmware keeps its log in a 4 KB RAM ring
(`app/fm1_log.c`) and the Device log card reads it over the same SysEx link: boot
and reset reason, update steps, BLE connections, task stack margins, and whatever
the SDK prints through its log hook. *Read* fetches everything held, *Follow* polls
once a second, *Save* downloads the text. The log survives nothing: a reset empties
it, but the health counters (Device card) do survive, and the first lines after a
boot say why it happened. The vendor library prints through the same hook (verified on
the first unit: LL, SM, VM and USB lines); its task and heap table, printed every minute,
is filtered out, since it would push everything else out of the ring within minutes.

## Backup and firmware

The Backup card exports the settings as a JSON file (`settings_hex` is the raw blob,
with its version and size) and imports one back into the form. The Firmware card takes
a `.fwsc` package and installs it with the same protocol as `tools/install.sh`: handshake,
step 1 served to the running firmware until it stages the loader and restarts, a wait
for the loader to enumerate (its identity starts with `ota-`), step 2 served to the
loader until it has written the app area, then a wait for the new firmware to come back
and report its identity. The page re-opens the MIDI ports as the device re-enumerates.
If a step stops, replug the cable and click Install again: the loader resumes where it
was and nothing is lost. Verified on the first unit 2026-10-20: a reinstall of `FM-1_931` from
Chrome, "done: FM-1_931", settings kept.
