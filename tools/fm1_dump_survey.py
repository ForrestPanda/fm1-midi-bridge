#!/usr/bin/env python3
"""Survey a 1 MB FM-1 flash dump: which 4 KB sectors hold anything but 0xFF, by region of the
flash map (docs/hardware.md), and which of them lie where a firmware writes (recovery.md,
"Before the first install": the SMK-37 keeps its factory calibration at 0xC1000 on the same chip;
losing such a sector is irreversible, so it must be mapped before anything writes there).

  fm1_dump_survey.py DUMP.bin [--all]

Prints one line per region, then every data sector outside the head and the app area with its
first bytes (--all: the app area's too). Exit 0: nothing unexpected; 2: a data sector in
0x94000..0xDFFFF (the region the drummer and the bridge write; stock keeps only its VM sector
at 0x93000 there); 1: not a 1 MB image.
First unit, 2026-10-19 (stock V15): data at 0x93000 (SDK VM, 55 AA AA 55), 0xE8000, 0xE9000 (BTIF),
ten sectors in 0xEA000..0xFB000; 0xFF000 blank; everything else 0xFF."""
import sys

SECTOR = 0x1000
REGIONS = [
    (0x00000, 0x04000, "flash head: SPL, isd_config.ini (never written)"),
    (0x04000, 0x93000, "app area (the install writes this)"),
    (0x93000, 0x94000, "VM: stock's settings sector"),
    (0x94000, 0xA0000, "data: projects, settings"),
    (0xA0000, 0xB0000, "USR1: the drummer's songs 1..8"),
    (0xB0000, 0xB4000, "the drummer's pedal settings"),
    (0xB4000, 0xC4000, "USR2: the drummer's songs 9..16 (across 0xC0000)"),
    (0xC4000, 0xE0000, "USR3, USR4, preset banks"),
    (0xE0000, 0xE5000, "update loader staging"),
    (0xE5000, 0xE9000, "FM6 bank, misc"),
    (0xE9000, 0xEA000, "BTIF"),
    (0xEA000, 0xFC000, "USR (stock's user data)"),
    (0xFC000, 0xFF000, "misc"),
    (0xFF000, 0x100000, "key_mac"),
]
WATCH = (0x94000, 0xE0000)      # a data sector here is the unit's own until proven otherwise


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 1
    show_all = "--all" in argv
    d = open(argv[1], "rb").read()
    if len(d) != 0x100000:
        print(f"{argv[1]}: {len(d)} bytes, not a 1 MB flash image")
        return 1
    used = [s for s in range(0, len(d), SECTOR) if any(b != 0xFF for b in d[s:s + SECTOR])]
    for lo, hi, name in REGIONS:
        here = [s for s in used if lo <= s < hi]
        span = f"  {here[0]:06X}..{here[-1]:06X}" if here else ""
        print(f"{lo:06X}-{hi - 1:06X}  {name:50s} {len(here):3d}/{(hi - lo) // SECTOR:<3d} sectors with data{span}")
    print()
    for s in used:
        if not show_all and 0x04000 <= s < 0x93000:
            continue
        blk = d[s:s + 16]
        text = "".join(chr(c) if 32 <= c < 127 else "." for c in blk)
        print(f"{s:06X}: {blk.hex(' ')}  {text}")
    odd = [s for s in used if WATCH[0] <= s < WATCH[1]]
    if odd:
        print(f"\nUNEXPECTED: {len(odd)} data sector(s) in {WATCH[0]:06X}..{WATCH[1] - 1:06X}: "
              + ", ".join(f"{s:06X}" for s in odd))
        print("Map them (hardware.md flash map) before a firmware writes there.")
        return 2
    print(f"\nnothing of the unit's in {WATCH[0]:06X}..{WATCH[1] - 1:06X}: a firmware may write there")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
