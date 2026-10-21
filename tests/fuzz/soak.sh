#!/bin/sh
# SPDX-License-Identifier: MIT
# Long fuzz run with a fresh seed, for cron or an idle machine:
#   sh tests/fuzz/soak.sh [iterations]        default 3000000 per harness (about half an hour)
# Appends one line per run to build/fuzz/soak.log; a failure keeps the run's full output
# in build/fuzz/soak-<seed>.log and the crashing input in build/fuzz/corpus_<harness>/.
# Crontab example (nightly at 03:00):
#   0 3 * * * cd /path/to/fm1-midi-bridge && sh tests/fuzz/soak.sh >/dev/null 2>&1
set -u
cd "$(dirname "$0")/../.."
iters=${1:-3000000}
seed=${FUZZ_SEED:-$(date +%s)}
out=build/fuzz; mkdir -p "$out"
log="$out/soak-$seed.log"
start=$(date +%s)
if FUZZ_SEED=$seed sh tests/fuzz/run_fuzz.sh "$iters" >"$log" 2>&1; then
    printf '%s seed %s iterations %s ok (%ss)\n' "$(date -Is)" "$seed" "$iters" "$(( $(date +%s) - start ))" >>"$out/soak.log"
    rm -f "$log"
    exit 0
fi
printf '%s seed %s iterations %s FAILED, see %s\n' "$(date -Is)" "$seed" "$iters" "$log" >>"$out/soak.log"
tail -5 "$log"
exit 1
