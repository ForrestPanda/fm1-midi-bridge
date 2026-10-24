# Avoiding a brick, and getting out of one

Walked on the first unit on 2026-10-19: what that unit showed is marked **verified**; the rest
is what the stock mechanism, Felucca's installer, the SDK sources and jl-uboot-tool say.
`tools/fm1_safe_install.sh` runs the pre-install steps and the install in order (below,
"Before the first install"); `docs/install.md` is the short version. The unit records below
also mention **BackBeat** ("the drummer", identity `FM-1_900`): the author's other FM-1
firmware, a SLOOP fork, which went onto the first unit before the bridge.

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

The script, from a shell on the computer the FM-1 is plugged into:

```
sh tools/fm1_safe_install.sh PACKAGE.fwsc            # step 2 below, then the install; a prompt before the power cycle
sh tools/fm1_safe_install.sh --dump-only             # a backup of the unit, nothing installed
sh tools/fm1_safe_install.sh PACKAGE.fwsc --no-dump  # this unit has its dump already
```

It needs `pip3 install --user mido python-rtmidi`, Felucca's installer and kagaimiq's
jl-uboot-tool checked out next to this clone (from the clone: `git clone
https://github.com/hugelton/Felucca ../Felucca`, `git clone https://github.com/kagaimiq/jl-uboot-tool
../jl-uboot-tool`, `pip3 install --user -r ../jl-uboot-tool/requirements.txt`; the scripts also look
one level further up, and `FM1_INSTALL=` / `JLUBOOT=` point elsewhere), and your password once: the
ROM disk `/dev/sgN` is root-only and the script opens it with `sudo chmod 666`. Linux only.
Dumps go to `fm1-dumps/` next to the clone (outside the repository; `FM1_DUMPS=` elsewhere), with
a `SHA256SUMS` line each; keep a second copy elsewhere, never commit or share them. The script does
step 2; the others are yours. The steps, and what the first unit did:

1. **Have a dongle ready (strongly recommended).** A microcontroller sends the 16-bit key
   0x16EF on D+/D- (D+ clock, D- data) at power-up, and the chip comes up in its USB download
   mode. It covers the one case nothing below does: a firmware that never enumerates, which
   happened to the first unit once (below, "The soft-lock"). Verified: an STM32F4-Discovery,
   `tools/dongle/stm32f4disco/` (`make flash` needs `arm-none-eabi-gcc` and `openocd`; wiring
   and use in `fm1_key.c`'s header). Untested: `tools/dongle/fm1_key.py` (any RP2040,
   MicroPython), kurogedelic's FM-1-transporter (XIAO RP2040), ip2k/mvave-fm1-open-firmware's
   dongle. The first unit's first installs went on without one (none at hand); a dongle was
   needed the same evening.
2. **Take a full dump.** Only a 1 MB dump holds the unit's own data: BTIF, the user
   data, VM. The stock package has none of it. The script: the identity of what runs
   (`fm1_install.py --info`), the soft key (`fm1_recover.py --softkey`), the dump
   (`fm1_flash.sh dump`, read twice and compared), the survey (`fm1_dump_survey.py`).
   **Verified 2026-10-19:** the soft key `F0 22 24 35 7D F7` drops stock V15 into the ROM
   within seconds; the ROM shows as USB 4c4a:8057 "WL80UBOOT1.00", SCSI model `WL82 UBOOT1.00`,
   `/dev/sg0` (root:disk) and a 0 B `/dev/sda`; the `sg` driver is built into the kernel here.
   jl-uboot-tool, which lists WL82 as untested, worked: its loader ran, it read the chip key 0x980F
   and the flash ID 0x856014, and read the 1 MB in 2 s, twice the same. The ROM has no software
   reset: switch the FM-1 off, unplug, wait 5 s, plug in, switch on, and stock is back.
   **Then read the dump before anything writes the data region** (added 2026-10-16, from the SMK-37
   port kit's flash safety guide, genkigenki/fm1-to-smk37: the same AC791N keeps the factory key and
   pad calibration `trimdat` at 0xC1000 and the Bluetooth pairing data at 0xC0000 on that board, and
   losing the calibration is irreversible). Our map (hardware.md) has BTIF at 0xE9000 and nothing of
   the unit's own below 0xE0000, but the two boards differ in their partition maps, and BackBeat's
   songs 9..16 live at 0xB4000..0xC3FFF, across 0xC0000. So: hexdump 0xC0000..0xC2FFF and the rest of
   0x93000..0xE0000 of the dump; sectors that are not 0xFF, or that carry a JLFS or trim header, are
   the unit's and must be mapped before the drummer (or the bridge's VM use) writes there. Record the
   finding in hardware.md's flash map either way. **The first unit (stock V15):** data only
   at 0x93000 (the SDK VM sector), 0xE8000, 0xE9000 (BTIF) and ten sectors in 0xEA000..0xFB000;
   0x94000..0xDFFFF blank, so nothing of the unit's where the drummer's songs go, and 0xFF000
   blank (the Bluetooth address is not there). The survey tool exits 2 on a data sector in
   0x94000..0xDFFFF and the script then asks before going on.
3. **Prove the stock path.** `tools/install.sh --info` on the untouched unit, then one
   reinstall of stock V15 (`tools/install.sh FM-1.fwsc`). If this fails on stock, stop here. (Skipped on the
   first unit: the handshake answered `FM-1_015` and the install from stock went through directly.)
4. **Charge, and keep the cable in.** Step 2 of an install (the loader rewriting the app
   area) is the only moment a power loss leaves the device without a working app.
5. **First flash the minimal build** (`make -C app MINIMAL=1`, `tools/package.sh --minimal`,
   identity `FM-1_930`): USB MIDI, TRS in, the updater, the editor and the log, no radio,
   no key matrix, no UI task. Reinstall it once (proves our step 1 on hardware), install
   stock back, then the full bridge. (The drummer went first, 2026-10-19: the bridge's turn
   starts with `--no-dump`, this unit's dump exists.)
6. **Close other MIDI programs first.** A sandboxed TuxGuitar held a subscription to the FM-1's
   ALSA port that `aconnect -d` could not drop; the install went through regardless, but SLOOP's
   advice stands: nothing else on the port during an update.

## The first unit, 2026-10-19

- Stock V15: USB 4c4a:c755 "FM-1", ALSA "FM-1 MIDI 1", identity `FM-1_015`.
- `fm1_install.py build/felucca.fwsc` (the drummer, `FM-1_900`) from stock, in one go: step 1
  (22 reads), the loader, step 2 to 100 %, restart, `FM-1_900` answered. About two minutes.
  BackBeat enumerates as ALSA "Felucca MIDI 1" plus a USB audio card "Felucca" (SLOOP's names,
  on purpose); its USB vendor id is not JieLi's 4c4a.
- A reinstall of the same package right after (step 5's "reinstall once"): BackBeat's own step 1
  worked and started the loader; the loader dropped off USB after 297 of about 1200 reads.
  Nothing had been written: the loader skips sectors that already match, and the image was the
  same. On the warm reset the SPL booted BackBeat again (the loader clears its RAM record first
  thing, by design: a loader crash cannot loop), it answered `FM-1_900` for about 40 s, then the
  unit was off until a power cycle 25 minutes later brought `FM-1_900` back. **Open:** why the
  loader dropped (an 8 s loader watchdog fed per sector, a USB glitch, the record BackBeat
  writes vs stock's). Retry the reinstall once with nothing else on the bus, and watch.
  **2026-10-22:** BackBeat over BackBeat (a newer build, TRS TO USB's BRIDGE ONLY) with `fm1_safe_install.sh
  --no-dump`, nothing else on the FM-1's port: the loader wrote to 100 %, restarted, `FM-1_900` answered. Not
  seen again; the cause of the 2026-10-19 drop stays unknown.
  If it happens mid-write of a *different* image, it leaves a half app; a power cycle
  (not a warm reset) brings the loader back from its flash record, and the install resumes.

### The bridge on the first unit, later the same day

Stock V15 was back on the unit (the drummer had been removed with the official package). Then,
over one cable, with Chrome's web installer tab still holding the FM-1's ALSA port:

- **Minimal build (`FM-1_930`) from stock, `fm1_safe_install.sh --no-dump`:** step 1 served by
  stock (21 reads), the loader, step 2 to 100 %, restart, `FM-1_930` answered. Enumerates as USB
  3654:4155 "M-VAVE FM-1", one MIDI streaming interface, ALSA "FM-1 MIDI 1" (so every installer
  finds it by name). `tools/fm1_editor.py` read its device log over SysEx: the SDK's printf reaches
  the ring, the VM is at 0x93000, the bridge task is up, `lcd init ok`, nine tasks, 407 KB heap free,
  no watchdog or hang counts. The packages had to be renamed first: `FM-1_9B1` / `FM-1_9B0` are not
  `FM-1_` + digits, which both Felucca's and the drummer's installers require (refused before anything
  was sent).
- **Reinstall of the minimal build, our updater's step 1: failed, nothing written.** The device log
  said `update step 1 code 0` then, 15 s later, `update step 9 code -1`: our entry asked the host for
  the first 512 bytes three times and never saw the reply. Cause: the host sends a reply (605 wire
  bytes, 808 bytes of USB-MIDI packets) as one bulk burst, the bridge's USB receive ring held 512
  bytes, and the update idle loop sleeps a 10 ms tick when the ring was last empty; the SDK's ring
  refuses a write that does not fit whole, so the reply's tail was lost and the frame never decoded.
  The staging area is only written after those reads succeed, so the unit kept running `FM-1_930`.
  Fixed: the ring is 2048 bytes (`app/bridge.c`, `USB_RING`). To get the fixed build onto a unit
  running the old one, `tools/fm1_install_paced.py` sends each SysEx as sequencer events of 48 bytes
  2 ms apart (the raw MIDI device is busy while Chrome holds the port; the sequencer is not). It
  installed the fixed minimal build through the old firmware's step 1 in one go.
- **Stock V15 back over the fixed build, unpaced, Felucca's installer** (it knows the official
  `FM-1.fwsc` by hash; the drummer's copy refuses it over a USR file CRC): our step 1, the loader,
  `FM-1_015` answered. So the way out of our firmware is proven, and the ring fix with it.
- **The full bridge (`FM-1_931`) from stock, unpaced:** `FM-1_931` answered; the log shows the BLE
  stack up and advertising as "FM-1 MIDI" (seen from the computer's Bluetooth), `lcd init ok`, the key
  matrix timer, USB up; 376 KB heap free, stack margins 457 (bridge) and 856 (UI) words, no watchdog
  or hang counts. **But the panel kept showing stock's last picture:** the SDK's "spi1" device
  moved nothing to the controller. Later that evening, with the register-level driver: keys,
  buttons and knobs answered on all eleven columns (`fm1_editor.py matrix`), a phone's BLE MIDI
  reached USB, and the screen-off had to move from PA2 to the panel's sleep command (PA2 is the
  595s' /OE: `hardware.md`). Open: two episodes of USB enumerated but silent until a power cycle.
- **The soft-lock (the ladder's rung 3, the same evening).** The panel driver rewritten at register
  level (Felucca's registers and values, `app/ui/lcd.c`), installed from `FM-1_931`: the loader
  wrote it, the unit restarted and **never came back on USB**; the screen went blank. Read: the
  register-level SWRESET did reach the panel (the stock picture is gone), and the next transfer,
  COLMOD's data byte, was handed to the SPI DMA as the address of a `const` byte in flash behind
  the SFC cache, which the DMA cannot read: the bus hangs, the 4 s hardware watchdog resets the
  chip, and the hang recurs at ~1.2 s of every boot, before USB is configured. No software door
  works on a unit that never enumerates: this is what the dongle is for. Fixed in the tree (small
  payloads through a RAM bounce buffer, bounded waits, the panel given up after three dead
  transfers, the UI skipping the panel after `HEALTH_SAFE_BOOTS` unclean boots in a row), built
  and host-tested, waiting for the unit. The dongle: `tools/dongle/fm1_key.py` (MicroPython, any
  RP2040, czietz's pins), or kurogedelic's FM-1-transporter on a XIAO RP2040 (it also restores).
  **Walked the same evening, all verified:**
  - The key from an STM32F4-Discovery (`tools/dongle/stm32f4disco/`, bare metal, openocd over the
    board's ST-Link): PD0 to D+ (green), PD1 to D- (white), GND to GND of a cut USB cable, nothing
    on VBUS, push-pull 3.3 V. The acknowledge was seen on the first try (orange LED), so the key
    and "D+ clock" are right. What then lost the ROM twice: ip2k's pulse-and-detect calibration
    (never run on hardware before) ended early, and a cable swap after the pulses had stopped. What
    worked: czietz's blind sequence, 1.5 s of key packets, 2 s of a 1 kHz square wave on D+, then
    unplug the board and plug the FM-1 into the computer right away; the ROM disk (4c4a:8057,
    `/dev/sg0`) was there on the second power-on. Without pull-ups on the lines the acknowledge
    cannot be told from the idle bus (both low), so the board keys blind in a loop and stops only
    when it sees D+ held high for 4 s (a live bus). `tools/dongle/fm1_key.py` is the same for an
    RP2040 in MicroPython, untested.
  - jl-uboot-tool's write path on the WL82: `read 0x4000 0x8f000` showed the broken app area, `write
    0x4000` of the stock dump's app area (erase, program) and a read-back `cmp` against the dump,
    then a power cycle: stock V15 answered `FM-1_015`. The head, VM, loader area and BTIF were not
    touched. About a minute in all.
  - The boot guard: a second register-level build (the SDK's "spi1" device opened first and a
    data-cache flush before each DMA) reset the chip twice at panel init (software resets, an
    exception), the third boot skipped the panel (`unclean 3`), USB came back, and the next build
    went on through the normal path, no dongle. The guard's record is in the SDK VM, which the
    app-area restore leaves alone.
  - Felucca's path alone (no SDK device, no cache call, payloads from RAM) ran clean: the minimal
    build showed its status lines, then the full bridge its Status page, BLE advertising, USB up,
    no resets. The panel driver as it is now is the one that works.
  **Lessons:** a driver change that can hang or fault the bus (DMA, bus masters, clocks) goes on
  through the minimal build with the panel guard in place, never straight over the full build;
  never DMA from a flash address; and keep the dongle wired up on the bench.

### Host mode on the first unit, 2026-10-19, evening

Seven installs of the full bridge in a row through its own update entry (`fm1_safe_install.sh
--no-dump`, about two minutes each), none lost: the loader drop-off seen on the reinstall of
BackBeat did not recur. Host mode tested against `tools/ble_midi_peripheral.py` on the PC;
the record of what worked and what was wrong is in `CHANGELOG.md` and `docs/plan.md`. Two
findings that matter for recovery: a USB bulk write with no reader on the PC blocked the
bridge loop (the stall monitor restarted the unit, `hangs` 2; fixed, the write is bounded),
and a stored host mode is not entered after a boot that never ran clean, so a host-mode
crash loop cannot keep the unit from its updater. The unit kept answering the editor between
every experiment; nothing needed the dongle.

## The ladder, cheapest first

### 1. The firmware runs and enumerates
Install stock with Felucca's installer: `tools/install.sh FM-1.fwsc` (M-VAVE's official V15
package, SHA-256 `db1642b2b6fa5c2cccb11ffd13878068bb28601678d3644049f99dc40e7edb8a`; the
installer refuses any other file without `--force`). **Verified** from the bridge on the first
unit. M-UPGRADE, M-VAVE's own updater, is untested on the bridge. Our firmware answers the
handshake like stock does (`tools/install.sh --info` shows `FM-1_931`, or `FM-1_930` for the
minimal build). If the device shows as `ota-FM-1_900` ("Felucca Update"), the loader our
packages carry is waiting: run the same install again, it resumes.

### 2. The firmware runs but the updater does not
Three doors into the mask ROM without hardware, all setting the soft key in NVRAM and
resetting:

- the System page, *USB recovery mode* (not yet tried on the unit);
- the SysEx soft key, which stock, Felucca and this bridge all honour:
  `F0 22 24 35 7D F7`  (`tools/fm1_recover.py --softkey`); **verified on stock V15 and on this
  bridge** (`FM-1_931`, 2026-10-20). Back out of the ROM: switch off, unplug, wait 5 s, plug in,
  switch on; with the cable in, the switch alone leaves the chip powered and in the ROM;
- the SDK's vendor control request on endpoint 0, `GET_STATUS` with wValue 0xFC and
  wIndex 0xFE, answered by any firmware built on the SDK USB stack even when the MIDI
  side is broken (`tools/fm1_recover.py --vendor`, needs pyusb and USB permission).

`tools/fm1_recover.py` with no option tries the last two and waits for the UBOOT disk.

### 3. The firmware never enumerates
Crash before USB, boot loop, dead screen. The dongle on D+/D- at power-up, then the app area
of your dump back: `tools/fm1_flash.sh restore-app ../fm1-dumps/fm1-FM-1_015-full-DATE.bin` (the
dump `fm1_safe_install.sh` took; head, VM and the unit's own data are not written). Power-cycle:
stock answers, and the install starts over. **Verified** on the first unit (the same write, by hand
in jl-uboot-tool's shell). `tools/fm1_flash.sh restore` writes the whole 1 MB and has never run:
a last resort, and only ever with this unit's own dump (another unit's carries its Bluetooth and
user data). Without a dump of your own there is no tested way back from here: take one first
(step 2). Another unit's app area is encrypted with that unit's chip key, which may differ.
The 4 s hardware watchdog and the 5 s bridge heartbeat restart the app, they do not fall
back to the ROM by themselves.

### 4. Last resort
Open the case and program the SPI NOR with a clip. There are no debug pads.

## Telling the states apart

| USB shows | State |
| --- | --- |
| MIDI device "FM-1" (USB 4c4a:c755), identity `FM-1_015` | stock (verified) |
| MIDI device "Felucca MIDI 1" + USB audio "Felucca", identity `FM-1_900` | BackBeat, the author's drummer firmware (verified) |
| MIDI device "FM-1" (USB 3654:4155), identity `FM-1_931` / `FM-1_930` | this bridge / its minimal build (verified) |
| MIDI device "Felucca Update" (USB 1209:0002), identity `ota-FM-1_900` | Felucca's update loader, which our packages carry, waiting for step 2: rerun the install |
| MIDI device, identity `ota-FM-1_015` | stock's own update loader (M-UPGRADE's), waiting for step 2 (from the protocol notes; not seen on the unit) |
| USB disk 4c4a:8057, SCSI model `WL82 UBOOT1.00`, `/dev/sgN` | mask ROM download mode: jl-uboot-tool territory (verified) |
| nothing | soft-locked or off: dongle |

## Early warnings

The System page and the web editor's Device card show the reset reason and the
watchdog / stall counters. A rising watchdog count while USB still works is the moment
to go back to stock before trying another build. The Device log card holds the last
4 KB of the firmware's own log, including the lines written just before an update or a
restart.

## What never to do

- Never write the flash head (0x0 to 0x4000). Our packager never does, nor does
  `restore-app`; `restore` writes it, so only ever from this unit's own dump.
- Never share your dump: it holds the unit's Bluetooth and user data.
- Never drive PB1 (the battery divider).
- Never change the VM region in the packager fork: it ends at 0xE0000 so the SDK's
  storage never touches the staged loader or the update record at 0xE4F00.
