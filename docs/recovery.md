# Avoiding a brick, and getting out of one

Nothing here has run on hardware yet (October 2026). It is what the stock mechanism,
Felucca's installer, the SDK sources and jl-uboot-tool say; verify each step on the
first unit and correct this file.

## Why the FM-1 is hard to brick

- The mask ROM in the AC791N is silicon. It can always be forced into its USB
  download mode (`UBOOT1.00`, a USB disk) by a key signal on D+/D- at power-up.
- The flash head (0x0 to 0x4000: SPL, `isd_config.ini` with the chip key) is never
  written by any update path: not by stock, not by Felucca's loader, not by ours.
- Everything an update writes lives in the app area and the loader staging area.
  A bad app is replaced by the next install; the loader at 0xE0000 is re-staged by
  the running firmware every time.

The realistic failure is a **soft-lock**: a firmware that boots but never answers the
update handshake over USB. Then the only way in is the mask ROM, from the outside.

## Before the first install

1. **Build the dongle first.** An RP2040 bit-bangs the 16-bit key 0x16EF over D+/D-
   (D- clock, D+ data) until the chip pulls both low, then hands the bus over.
   Spec and firmware: ip2k/mvave-fm1-open-firmware (`dongle/`). With it on the desk,
   every failure below is an inconvenience.
2. **Take a full dump.** Only a 1 MB dump holds the unit's own data: the Bluetooth
   address at 0xFF000, BTIF, VM. The stock package has none of it.
   `tools/fm1_recover.py` then `tools/fm1_flash.sh dump fm1-stock-full.bin`.
3. **Prove the stock path.** `tools/install.sh --info` on the untouched unit, then one
   reinstall of stock v15 with M-UPGRADE. If this fails on stock, stop here.
4. **Charge, and keep the cable in.** Step 2 of an install (the loader rewriting the app
   area) is the only moment a power loss leaves the device without a working app.
5. **First flash the minimal build** (`make -C app MINIMAL=1`, `tools/package.sh --minimal`,
   identity `FM-1_9B0`): USB MIDI, TRS in, the updater, the editor and the log, no radio,
   no key matrix, no UI task. Reinstall it once (proves our step 1 on hardware), install
   stock back, then the full bridge.

## The ladder, cheapest first

### 1. The firmware runs and enumerates
Install stock with M-UPGRADE or `tools/install.sh`. Our firmware answers the handshake
like stock does (`tools/install.sh --info` shows `FM-1_9B1`, or `FM-1_9B0` for the
minimal build). If the device shows as `ota-FM-1_015`, the loader is waiting: run the
install again, it resumes.

### 2. The firmware runs but the updater does not
Three doors into the mask ROM without hardware, all setting the soft key in NVRAM and
resetting:

- the System page, *USB recovery mode*;
- the SysEx soft key, which stock, Felucca and this bridge all honour:
  `F0 22 24 35 7D F7`  (`tools/fm1_recover.py --softkey`);
- the SDK's vendor control request on endpoint 0, `GET_STATUS` with wValue 0xFC and
  wIndex 0xFE, answered by any firmware built on the SDK USB stack even when the MIDI
  side is broken (`tools/fm1_recover.py --vendor`, needs pyusb and USB permission).

`tools/fm1_recover.py` with no option tries the last two and waits for the UBOOT disk.

### 3. The firmware never enumerates
Crash before USB, boot loop, dead screen. The dongle on D+/D- at power-up, then
`tools/fm1_flash.sh restore fm1-stock-full.bin` (or the stock `flash.bin` out of the
official package, which holds head + app area and leaves the unit data as it is). The
4 s hardware watchdog and the 5 s bridge heartbeat restart the app, they do not fall
back to the ROM by themselves.

### 4. Last resort
Open the case and program the SPI NOR with a clip. There are no debug pads.

## Telling the states apart

| USB shows | State |
| --- | --- |
| MIDI device "FM-1", identity `FM-1_015` | stock |
| MIDI device "FM-1", identity `FM-1_9B1` / `FM-1_9B0` | this bridge / its minimal build |
| MIDI device, identity `ota-FM-1_015` | the update loader, waiting for step 2 |
| USB disk, SCSI model `UBOOT1.00` | mask ROM download mode: jl-uboot-tool territory |
| nothing | soft-locked or off: dongle |

## Early warnings

The System page and the web editor's Device card show the reset reason and the
watchdog / stall counters. A rising watchdog count while USB still works is the moment
to go back to stock before trying another build. The Device log card holds the last
4 KB of the firmware's own log, including the lines written just before an update or a
restart.

## What never to do

- Never write the flash head (0x0 to 0x4000). Our packager never does; `restore` writes
  the head only from a dump of the same unit or the stock image, which are the same bytes.
- Never drive PB1 (the battery divider).
- Never change the VM region in the packager fork: it ends at 0xE0000 so the SDK's
  storage never touches the staged loader or the update record at 0xE4F00.
