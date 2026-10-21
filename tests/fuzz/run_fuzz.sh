#!/bin/sh
# SPDX-License-Identifier: MIT
# Fuzz the parsers that run in the bridge task, and the bridge task itself, under ASan + UBSan.
#   sh tests/fuzz/run_fuzz.sh [iterations]        default 200000 per harness, ~2 min in all
# FUZZ_SEED=n picks another random sequence (see driver.c); a crash leaves its input in
# build/fuzz/corpus_<harness>/ and the next run replays it first. tests/fuzz/soak.sh runs
# this with many iterations and a fresh seed, for cron.
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
BRIDGE="../host/bridge_sim.c ../../app/bridge.c ../../app/editor.c ../../app/settings.c ../../app/util/crc16.c ../../app/util/pack7.c ../../app/util/logring.c ../../app/midi/midi_msg.c ../../app/midi/usbmidi.c ../../app/midi/blemidi.c ../../app/midi/router.c ../../app/midi/panic.c ../../app/midi/merge.c ../../app/midi/stats.c ../../app/midi/recorder.c"
BRIDGE_FLAGS="-I../host/sdk -I../host -Wno-pointer-to-int-cast"      # the SDK ioctl takes a u32 where bridge.c passes a pointer
run() {   # name extra-flags sources...
    name=$1; extra=$2; shift 2
    if [ "${FUZZ:-}" = "libfuzzer" ]; then
        $CC $FLAGS $extra -fsanitize=fuzzer -o "$out/$name" "$name.c" "$@"
        "$out/$name" -max_total_time="${FUZZ_SECONDS:-30}" -max_len=2048 "$out/corpus_$name" 2>&1 | tail -2
    else
        $CC $FLAGS $extra -o "$out/$name" "$name.c" driver.c "$@"
        "$out/$name" "$iters" "$out/corpus_$name"
    fi
}
mkdir -p "$out/corpus_fuzz_midi" "$out/corpus_fuzz_pack7" "$out/corpus_fuzz_editor" "$out/corpus_fuzz_update" "$out/corpus_fuzz_bridge"
run fuzz_midi "" $MIDI
run fuzz_pack7 "" $PACK
run fuzz_editor "" $EDITOR
run fuzz_update "" $UPDATE
run fuzz_bridge "$BRIDGE_FLAGS" $BRIDGE
echo "fuzz: all harnesses ran clean"
