#!/bin/sh
# SPDX-License-Identifier: MIT
# Whole-flash backup and restore of an FM-1 in mask-ROM USB download mode (UBOOT1.00),
# through kagaimiq's jl-uboot-tool (MIT). Get it next to this clone or point JLUBOOT at it:
#   git clone https://github.com/kagaimiq/jl-uboot-tool ../jl-uboot-tool
#   pip3 install --user -r ../jl-uboot-tool/requirements.txt
#
#   tools/fm1_flash.sh dump FILE         read all 1 MB (head, app area, VM, loader area, BTIF, key_mac) into FILE
#   tools/fm1_flash.sh verify FILE       read again and compare with FILE
#   tools/fm1_flash.sh restore-app FILE  write only the app area (0x4000..0x93000) out of the 1 MB dump FILE,
#                                        then read it back and compare: the way back from a firmware that never
#                                        enumerates; head, VM and the unit's own data are left as they are
#   tools/fm1_flash.sh restore FILE      write all of FILE from 0, then read back and compare (last resort;
#                                        only ever this unit's own dump: another unit's carries its BTIF and USR)
#   tools/fm1_flash.sh shell             the tool's own shell
# Env: JLUBOOT (tool checkout; default next to this clone, or one level up), DEVICE (/dev/sgN; the tool
# finds it when unset). The chip must already be in UBOOT mode: tools/fm1_recover.py, or the D+/D- dongle.
# Verified on the first unit (2026-10-19): dump (read twice, identical), and the app-area write that
# restore-app does, typed by hand in jl-uboot-tool's shell then. restore (the whole flash) has never run.
# A dump taken before the first install is the only backup that holds the unit's own data.
set -e
here=$(cd "$(dirname "$0")/.." && pwd)
sibling() {   # $1 checked out next to this clone, or one level further up; the first that exists
    for d in "$here/.." "$here/../.."; do [ -d "$d/$1" ] && { (cd "$d/$1" && pwd); return; }; done
    echo "$here/../$1"
}
JLUBOOT=${JLUBOOT:-$(sibling jl-uboot-tool)}
tool="$JLUBOOT/jluboottool.py"
[ -f "$tool" ] || { echo "fm1_flash.sh: $tool missing (see the header of this script)"; exit 1; }
dev=""; [ -n "${DEVICE:-}" ] && dev="--device $DEVICE"
SIZE=0x100000
cmd=$1; file=$2
case "$cmd" in
dump)
    [ -n "$file" ] || { echo "usage: fm1_flash.sh dump FILE"; exit 1; }
    [ ! -e "$file" ] || { echo "fm1_flash.sh: $file exists, not overwriting a backup"; exit 1; }
    python3 "$tool" $dev "read 0 $SIZE $file"
    python3 "$tool" $dev "read 0 $SIZE $file.verify"
    cmp "$file" "$file.verify" && rm -f "$file.verify" && echo "dump ok: $file ($(wc -c < "$file") bytes, read twice, identical)"
    ;;
verify)
    [ -f "$file" ] || { echo "usage: fm1_flash.sh verify FILE"; exit 1; }
    python3 "$tool" $dev "read 0 $SIZE $file.verify"
    cmp "$file" "$file.verify" && rm -f "$file.verify" && echo "flash matches $file"
    ;;
restore-app)
    [ -f "$file" ] || { echo "usage: fm1_flash.sh restore-app DUMP"; exit 1; }
    [ "$(wc -c < "$file")" -eq 1048576 ] || { echo "fm1_flash.sh: $file is not a 1 MB flash dump"; exit 1; }
    app=$(mktemp); back=$(mktemp); trap 'rm -f "$app" "$back"' EXIT
    dd if="$file" of="$app" bs=4096 skip=4 count=143 status=none          # 0x4000..0x93000, 0x8F000 bytes
    echo "This writes the app area (0x4000..0x93000) of the connected chip from $file."
    printf "Type 'restore' to continue: "; read -r ans; [ "$ans" = "restore" ] || exit 1
    python3 "$tool" $dev "write 0x4000 $app"
    python3 "$tool" $dev "read 0x4000 0x8f000 $back"
    cmp "$app" "$back" && echo "app area restored and verified; power-cycle the FM-1 (off, unplug, wait 5 s, plug in, on)"
    ;;
restore)
    [ -f "$file" ] || { echo "usage: fm1_flash.sh restore FILE"; exit 1; }
    [ "$(wc -c < "$file")" -eq 1048576 ] || { echo "fm1_flash.sh: $file is not a 1 MB flash image"; exit 1; }
    echo "This writes the whole flash of the connected chip from $file (head, app, data)."
    printf "Type 'restore' to continue: "; read -r ans; [ "$ans" = "restore" ] || exit 1
    python3 "$tool" $dev "write 0 $file"
    python3 "$tool" $dev "read 0 $SIZE $file.verify"
    cmp "$file" "$file.verify" && rm -f "$file.verify" && echo "restore ok and verified; power-cycle the FM-1"
    ;;
shell)
    exec python3 "$tool" $dev
    ;;
*)
    sed -n 2,21p "$0"; exit 1
    ;;
esac
