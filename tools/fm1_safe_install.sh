#!/bin/sh
# SPDX-License-Identifier: MIT
# The first install of a firmware on an FM-1, the way docs/recovery.md orders it, over one USB cable:
#   1. the identity of what runs                      (fm1_install.py --info)
#   2. the SysEx soft key: the chip's ROM download mode  (fm1_recover.py --softkey; stock V15 honours it)
#   3. a full 1 MB dump, read twice and compared, SHA-256 recorded  (fm1_flash.sh dump)
#   4. a survey of the dump: what is the unit's own, is any of it where the firmware writes  (fm1_dump_survey.py)
#   5. you power-cycle the FM-1 (the ROM has no software reset); the same identity must be back
#   6. the install, then the identity of what runs      (fm1_install.py PACKAGE --yes)
# Nothing is written before step 6, and step 6 writes only the app area through the firmware's own
# update path. Walked on the first unit on 2026-10-19: stock V15 -> BackBeat FM-1_900 (recovery.md).
#
#   tools/fm1_safe_install.sh PACKAGE.fwsc [--no-dump | --dump-only] [--port NAME] [--force]
#     --no-dump    skip 2..5 (this unit has its dump already)
#     --dump-only  stop after 5: a backup, nothing installed (PACKAGE may be omitted)
#     --port NAME  the MIDI port, when the default match is not enough (passed to the tools)
#     --force      a package without the Felucca loader marker (stock V15: going back)
# Env: FM1_INSTALL  the fm1_install.py to use (default: fm1-backbeat's copy, else Felucca's)
#      FM1_DUMPS    where dumps go (default: my_fw/fm1-dumps; never commit them, keep a second copy elsewhere)
#      JLUBOOT      the jl-uboot-tool checkout (default and setup: the header of fm1_flash.sh)
# Needs: python3 with mido and python-rtmidi; jl-uboot-tool with its requirements; root for the ROM disk
# (/dev/sgN is root:disk): the script runs `sudo chmod 666` on it and sudo asks for your password.
set -e
here=$(cd "$(dirname "$0")/.." && pwd)
top=$(cd "$here/.." && pwd)                       # my_fw
INSTALL=${FM1_INSTALL:-$top/fm1-backbeat/tools/fm1_install.py}
[ -f "$INSTALL" ] || INSTALL=$top/../Felucca/tools/fm1_install.py
DUMPS=${FM1_DUMPS:-$top/fm1-dumps}

pkg=""; dump=1; install=1; port=""; force=""
while [ $# -gt 0 ]; do
    case "$1" in
    --no-dump) dump=0 ;;
    --dump-only) install=0 ;;
    --port) port="--port $2"; shift ;;
    --force) force="--force" ;;
    -h|--help) sed -n 2,21p "$0"; exit 0 ;;
    -*) echo "fm1_safe_install.sh: unknown option $1 (--help)"; exit 2 ;;
    *) pkg=$1 ;;
    esac
    shift
done
[ $install -eq 0 ] || [ -n "$pkg" ] || { echo "usage: fm1_safe_install.sh PACKAGE.fwsc [--no-dump | --dump-only]"; exit 2; }
[ $install -eq 0 ] || [ -f "$pkg" ] || { echo "fm1_safe_install.sh: $pkg: no such package"; exit 2; }
[ $dump -eq 1 ] || [ $install -eq 1 ] || { echo "fm1_safe_install.sh: --no-dump with --dump-only does nothing"; exit 2; }

say() { printf '\n== %s\n' "$*"; }
die() { printf 'fm1_safe_install.sh: %s\n' "$*" >&2; exit 1; }

# -- prerequisites, before anything talks to the device
[ -f "$INSTALL" ] || die "no fm1_install.py (FM1_INSTALL)"
python3 -c "import mido, rtmidi" 2>/dev/null || die "pip3 install mido python-rtmidi"
if [ $dump -eq 1 ]; then
    JLUBOOT=${JLUBOOT:-$here/../../jl-uboot-tool}
    [ -f "$JLUBOOT/jluboottool.py" ] || die "jl-uboot-tool missing at $JLUBOOT (the header of tools/fm1_flash.sh)"
    python3 -c "import scsiio, crcmod, yaml, Cryptodome, tqdm" 2>/dev/null \
        || die "pip3 install --user -r $JLUBOOT/requirements.txt"
    export JLUBOOT
fi

identity_of() {       # prints "IDENTITY MODE" of the connected FM-1, or nothing
    python3 "$INSTALL" --info $port 2>/dev/null | sed -n 's/^\([^ ]*\)  *\[\([a-z]*\).*/\1 \2/p'
}
wait_identity() {     # up to $1 seconds for a running firmware; prints its identity
    n=$1
    while [ "$n" -gt 0 ]; do
        set -- $(identity_of)
        if [ "$2" = "running" ]; then echo "$1"; return 0; fi
        sleep 2; n=$((n - 2))
    done
    return 1
}

# -- 1. what runs
say "1. the firmware that runs"
set -- $(identity_of)
[ -n "$1" ] || die "no FM-1 on USB answers the handshake (cable? another program on its MIDI port? --port?)"
before=$1
[ "$2" = "running" ] || die "$before is the update loader waiting for the rest of an install: python3 $INSTALL PACKAGE finishes it first"
echo "   $before"

if [ $dump -eq 1 ]; then
    # -- 2. the ROM
    say "2. the soft key: into the chip's ROM download mode (nothing is written there)"
    out=$(python3 "$here/tools/fm1_recover.py" --softkey --timeout 40 $port) || { echo "$out"; die "no UBOOT disk: docs/recovery.md, the ladder"; }
    echo "$out" | tail -1
    sg=$(echo "$out" | sed -n 's/.* at \(\/dev\/sg[0-9]*\).*/\1/p' | tail -1)
    [ -n "$sg" ] || die "could not tell the ROM disk's /dev/sgN from: $out"
    if [ ! -w "$sg" ]; then
        echo "   $sg is root-only; opening it for this session: sudo chmod 666 $sg"
        sudo chmod 666 "$sg" || die "no permission on $sg"
    fi

    # -- 3. the dump
    say "3. the full flash, read twice"
    mkdir -p "$DUMPS"
    file="$DUMPS/fm1-$before-full-$(date +%Y-%m-%d).bin"
    [ ! -e "$file" ] || file="$DUMPS/fm1-$before-full-$(date +%Y-%m-%d-%H%M%S).bin"
    out=$(DEVICE=$sg sh "$here/tools/fm1_flash.sh" dump "$file" 2>&1) || true
    echo "$out" | grep -v -E 'Reading: +[0-9]' | tail -3
    echo "$out" | grep -q '^dump ok' || die "the two reads differ or the tool failed: nothing to trust, run again"
    (cd "$DUMPS" && sha256sum "$(basename "$file")" >> SHA256SUMS && tail -1 SHA256SUMS)
    echo "   keep a second copy of $file somewhere else"

    # -- 4. the survey
    say "4. what the dump holds"
    if ! python3 -I "$here/tools/fm1_dump_survey.py" "$file"; then
        printf '   Continue anyway? Only if the sectors above are mapped and the firmware leaves them alone. [y/N] '
        read -r ans; [ "$ans" = "y" ] || exit 1
    fi

    # -- 5. back to the firmware
    say "5. power-cycle the FM-1: switch it off, unplug the USB, wait 5 s, plug it back in, switch it on"
    printf '   then press Enter: '; read -r ans
    back=$(wait_identity 60) || die "nothing answers after the power cycle: unplug and replug, then run again with --no-dump"
    [ "$back" = "$before" ] || die "the firmware that came back is $back, not $before: stop and look"
    echo "   $back is back"
    [ $install -eq 1 ] || { say "done: the backup is $file, nothing was installed"; exit 0; }
fi

# -- 6. the install
say "6. installing $pkg (keep the cable in: the loader's write is the one moment a power loss matters)"
if python3 "$INSTALL" "$pkg" --yes $force $port; then rc=0; else rc=$?; fi
if [ $rc -ne 0 ]; then
    cat <<MSG
   The install did not finish (exit $rc). Nothing below the app area was touched.
   - If the FM-1 still shows on USB: run  python3 $INSTALL $pkg  again, it resumes.
   - If it does not: power-cycle it (the loader comes back from its flash record), then run the install again.
   - Then docs/recovery.md, the ladder: the soft key, the vendor request, the ROM disk, the dump.
MSG
    exit $rc
fi
say "7. what runs now"
after=$(wait_identity 60) || die "no answer after the install: unplug and replug, then python3 $INSTALL --info"
echo "   $after  (was $before)"
