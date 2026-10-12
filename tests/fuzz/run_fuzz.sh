#!/bin/sh
# Fuzz the parsers that run in the bridge task, under ASan + UBSan.
#   sh tests/fuzz/run_fuzz.sh [iterations]        default 200000 per harness, ~1 min in all
# With clang and a working libFuzzer (FUZZ=libfuzzer), each harness runs libFuzzer for
# FUZZ_SECONDS (default 30) instead; the harnesses are the same.
set -e
cd "$(dirname "$0")"
iters=${1:-200000}
out=../../build/fuzz; mkdir -p "$out"
CC=${CC:-clang}; command -v "$CC" >/dev/null || CC=cc
FLAGS="-std=c99 -Wall -Wextra -Wpedantic -Werror -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer -I../../app -DFM1_FW_VERSION=\"fuzz\""
MIDI="../../app/midi/midi_msg.c ../../app/midi/usbmidi.c ../../app/midi/blemidi.c ../../app/midi/router.c ../../app/midi/decode.c ../../app/midi/stats.c ../../app/midi/merge.c ../../app/midi/recorder.c"
EDITOR="../../app/editor.c ../../app/settings.c ../../app/midi/router.c ../../app/util/crc16.c ../../app/util/pack7.c ../../app/util/logring.c"
UPDATE="../../app/update/fm1_update.c ../../app/util/crc16.c ../../app/util/pack7.c"
PACK="../../app/util/pack7.c"
run() {   # name sources...
    name=$1; shift
    if [ "${FUZZ:-}" = "libfuzzer" ]; then
        $CC $FLAGS -fsanitize=fuzzer -o "$out/$name" "$name.c" "$@"
        "$out/$name" -max_total_time="${FUZZ_SECONDS:-30}" -max_len=2048 "$out/corpus_$name" 2>&1 | tail -2
    else
        $CC $FLAGS -o "$out/$name" "$name.c" driver.c "$@"
        "$out/$name" "$iters" "$out/corpus_$name"
    fi
}
mkdir -p "$out/corpus_fuzz_midi" "$out/corpus_fuzz_pack7" "$out/corpus_fuzz_editor" "$out/corpus_fuzz_update"
run fuzz_midi $MIDI
run fuzz_pack7 $PACK
run fuzz_editor $EDITOR
run fuzz_update $UPDATE
echo "fuzz: all harnesses ran clean"
