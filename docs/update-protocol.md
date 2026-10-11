# Getting firmware onto the FM-1 and staying updatable

Sources: aroum/fm1-custom-fw README (M-UPGRADE reverse engineering), Felucca
`firmware/src/ota.c`, `firmware/loader/`, `tools/fm1pkg_make.py`, `tools/fm1_install.py`.

## The stock mechanism

The only update channel is USB-MIDI SysEx. There is no DFU, no debug port.

1. **Handshake.** Host sends `F0 00 32 45 00 00 00 40 7F F7`. The running firmware
   answers with its identity (`F0 00 32 45 58 ... "MZD..." ... F7`), which includes the
   package identity string (stock `FM-1_xxx`, Felucca `FM-1_909`).
2. **Step 1, in the running app.** Host sends the upgrade command
   `F0 22 24 35 7F F7`. The app then *pulls* parts of the package from the host with
   cmd `0x30` read requests (`F0 pack7(00 59 cmd len24 body chk) F7` framing, 7-bit
   packed), stages a small **update loader** (stock: `usb_hid_ota.bin`, 0x4DE1 bytes,
   JieLi's own) in flash, asks the host for `0xE0000000` ("success"), writes a
   112-byte `UPDATA_PARM` record (RAM `0x01C7FD88` + a flash copy) and resets.
3. **Step 2, in the loader.** The SPL (`uboot.boot`) sees the record and runs the
   loader, which pulls the whole package the same way, writes the app area
   `[0x4000, 0x93000)` sector by sector, asks `0xF0000000`, clears the record and resets.
4. **Package.** A `.fwsc` is a JieLi UFW container (`@JMUA` / `JLUFW` magic, chip
   `AC791N`) holding `flash.bin` (full flash image with head + SFC-encrypted app area)
   and `ota.bin` (the loader). CRC16 poly 0x1021 init 0 everywhere. No signatures.

This is JieLi's standard single-bank "loader" OTA, with the transport swapped from
USB HID to USB MIDI SysEx. The SDK ships the update framework and loader as part of
its binary libs; **finding the SDK's hook for a custom transport is the first
research task** (see plan.md). If that hook exists, our app gets step 1 from vendor
code and only needs a SysEx-over-USB-MIDI transport shim.

## Hard rule

The app **must** implement step 1 (answer the handshake, honour the upgrade command,
stage a loader and reset into it). A firmware without it soft-locks the device:
the only way back is opening the case / the UBOOT dongle.

## Loader choice

- Stock `usb_hid_ota.bin`: in every stock `.fwsc` and in the M-UPGRADE app. Known
  CRCs (Felucca `ota.c`: header CRC 0xEBAA, data CRC 0x5881, length 0x4DE1).
  Redistribution rights unclear.
- Felucca's loader (GPL-3): a separate program, not linked with our app. Shipping it
  inside our package is aggregation; we must provide its source alongside.
- SDK loader: if the SDK tree has one that speaks the same 0x30 read protocol
  (unlikely, the protocol is M-VAVE's), use it.

## Installer

Felucca's `tools/fm1_install.py` and web installer speak the whole protocol and
install any conforming `.fwsc`; use them as-is (GPL tools, not linked). A package
built for the official identity (`FM-1_` prefix) is what the stock M-UPGRADE accepts.

## Software recovery entry (from the SDK, no dongle)

The SDK's USB device code (`apps/common/usb/device/user_setup.c`, compiled into our
app) answers a vendor control request on EP0: `GET_STATUS` with `wValue 0xFC`,
`wIndex 0xFE`. With `wLength 8` the device replies `FF x 8` ("supported"); with
`wLength 0` it calls `go_mask_usb_updata()` (`msd_upgrade.c`): sets the boot state to
`UPGRADE_USB_SOFT_KEY` in NVRAM and resets, so the mask ROM comes up in USB download
mode (`UBOOT1.00`). That is the same soft key Felucca implements over SysEx
(`fm1_enter_uboot`). As long as our app enumerates on USB, a host tool can put the
FM-1 into recovery without the RP2040 dongle. Verify on hardware before relying on it,
and keep the dongle route as the fallback for a firmware that never enumerates.

## Going back to stock

M-VAVE's own updater (M-UPGRADE) restores the official firmware as long as step 1
works in whatever is running. Keep the stock `FM-1.fwsc` (v15, 2026-07-30) on disk.
