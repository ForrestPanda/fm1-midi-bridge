# BLE MIDI 1.0 in one page

Spec: MIDI Association, "Specification for MIDI over Bluetooth Low Energy (BLE-MIDI)",
version 1.0, 2015. Apple's "Bluetooth Low Energy MIDI Specification" is the same.

## GATT

- Service UUID: `03B80E5A-EDE8-4B33-A751-6CE34EC4C700`
- Characteristic UUID: `7772E5DB-3868-4112-A1A9-F2669D106BF3`
  - properties: Read, Write Without Response, Notify
  - a read returns an empty payload
  - the central subscribes (CCCD) to receive notifications
- Advertise the service UUID in the advertising data (iOS and Android filter on it).
- Connection interval: request the smallest the central allows (Apple: 15 ms is
  what macOS/iOS grant; 7.5 ms via the central if it agrees). MTU: negotiate larger
  than 23 to fit chords; many centrals give 100+.
- Security: iOS requires pairing/bonding for MIDI apps to see the device; use
  "just works" with bonding so reconnects are silent.

## Packet format

Each notification / write carries one packet:

```
header     1 byte   10tttttt   t = timestampHigh (bits 12..7 of a 13-bit ms clock)
timestamp  1 byte   1ttttttt   t = timestampLow (bits 6..0), precedes each MIDI status
status     1 byte   1sssssss
data       0..2     0ddddddd
```

- Timestamps are a 13-bit millisecond counter (wraps every 8192 ms), relative to the
  sender; the receiver only uses them to reproduce spacing inside a packet.
- Running status is allowed *within* a packet: after a full message, data bytes
  without a new timestamp and status continue the previous status. A new timestamp
  byte always starts a new message.
- System real-time (F8..FF) can be interleaved anywhere, each preceded by a
  timestamp.
- SysEx: F0 with a timestamp, data bytes, then F7 with its own timestamp. A SysEx
  may span packets; the continuation packets start with the header and data bytes
  only (no timestamp) until the F7.
- A packet must not exceed MTU - 3 bytes.

## What the bridge does

- **TRS -> BLE:** a UART byte parser (same logic as a MIDI thru box: handle running
  status, real-time, SysEx) feeds a packetizer that batches messages arriving within
  one connection interval into one notification, with correct per-message timestamps.
- **BLE -> USB:** parse packets into MIDI messages, re-encode as USB-MIDI 4-byte event
  packets (cable 0, CIN from the status byte).
- **USB -> BLE:** the reverse.
- Realtime clock (F8) over BLE is jittery; forward it but do not expect sync quality.
