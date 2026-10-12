#!/bin/sh
# Whole-flash backup and restore of an FM-1 in mask-ROM USB download mode (UBOOT1.00),
# through kagaimiq's jl-uboot-tool (MIT). Get it next to this tree or point JLUBOOT at it:
#   git clone https://github.com/kagaimiq/jl-uboot-tool ../../jl-uboot-tool
#   pip3 install -r ../../jl-uboot-tool/requirements.txt
#
#   tools/fm1_flash.sh dump FILE       read all 1 MB (head, app area, VM, loader area, BTIF, key_mac) into FILE
#   tools/fm1_flash.sh verify FILE     read again and compare with FILE
#   tools/fm1_flash.sh restore FILE    write FILE to flash from 0, then read back and compare
#   tools/fm1_flash.sh shell           the tool's own shell
# Env: JLUBOOT (tool checkout), DEVICE (/dev/sgN; the tool finds it when unset).
# The chip must already be in UBOOT mode: tools/fm1_recover.py, or the D+/D- dongle.
# jl-uboot-tool lists the WL82 family (AC791N) as untested; nothing here is verified
# on hardware yet (October 2026). A dump taken before the first install is the only
# backup that holds the unit's own data (Bluetooth address, pairing records).
set -e
here=$(cd "$(dirname "$0")/.." && pwd)
JLUBOOT=${JLUBOOT:-$here/../../jl-uboot-tool}
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
    sed -n 2,13p "$0"; exit 1
    ;;
esac
