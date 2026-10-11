#!/bin/sh
# app.bin = .text + .data + .ram0_data + .cache_ram_data of the ELF, as the SDK's
# download.bat does (cpu/wl82/tools/download.c).  mkapp.sh ELF OUT.bin
set -e
elf="$1"; out="$2"; dir=$(dirname "$out")
OBJCOPY=${OBJCOPY:-$HOME/.jieli/toolchain/pi32v2/bin/objcopy}
for s in text data ram0_data cache_ram_data; do
    "$OBJCOPY" -O binary -j .$s "$elf" "$dir/sec_$s.bin"
done
cat "$dir/sec_text.bin" "$dir/sec_data.bin" "$dir/sec_ram0_data.bin" "$dir/sec_cache_ram_data.bin" > "$out"
rm -f "$dir"/sec_*.bin
ls -l "$out"
