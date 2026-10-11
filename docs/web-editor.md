# Web editor

`web/editor.html` edits every setting from a browser over USB-MIDI, using Web MIDI
(Chrome or Edge). Serve it from localhost, since Web MIDI needs a secure context:

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
| 0x09 + port + pack7(bytes) | 0x49 | send raw MIDI bytes to port 1 = USB, 2 = BLE, 3 = both, bypassing routes |

pack7 is the same 8-to-7, LSB-first packing the M-VAVE update protocol uses. The
settings blob is `fm1_settings` from `app/settings.h` byte for byte; the page carries the
offsets of the current version and `tests/test_editor.c` asserts them, so a layout
change fails the host tests until both are updated. The device keeps its own
`host_peer` field (the last BLE host-mode device) and recomputes the CRC on write.

## Backup and firmware

The Backup card exports the settings as a JSON file (`settings_hex` is the raw blob,
with its version and size) and imports one back into the form. The Firmware card takes
a `.fwsc` package and installs it with the same protocol as `tools/install.sh`: handshake,
step 1 served to the running firmware until it stages the loader and restarts, a wait
for the loader to enumerate (its identity starts with `ota-`), step 2 served to the
loader until it has written the app area, then a wait for the new firmware to come back
and report its identity. The page re-opens the MIDI ports as the device re-enumerates.
If a step stops, replug the cable and click Install again: the loader resumes where it
was and nothing is lost. Untested on hardware until the first unit arrives.
