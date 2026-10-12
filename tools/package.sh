#!/bin/sh
# Package build/app.bin into an installable FM-1 package, using Felucca's tools as
# external programs (GPL-3 tools run at build time; nothing of them is linked into
# our firmware):
#   - tools/fm1pkg_make.py  (our fork of Felucca's) builds the UFW container: flash image (head + SFC-encrypted
#                           app area) + the update loader
#   - build/loader/ota.bin  Felucca's update loader (step 2 of the install), a separate
#                           program the SPL runs; its source is in the Felucca checkout
#
#   tools/package.sh [IDENTITY]       default identity FM-1_9B1 (FM-1_9xx: custom firmware)
#   tools/package.sh --minimal        build/minimal/app.bin -> build/fm1bridge-minimal.fwsc, identity FM-1_9B0
#
# Env: FELUCCA (default ../../Felucca), AC79_SDK (default ~/fw-AC79_AIoT_SDK)
set -e
here=$(cd "$(dirname "$0")/.." && pwd)
FELUCCA=${FELUCCA:-$here/../../Felucca}
export AC79_SDK=${AC79_SDK:-$HOME/fw-AC79_AIoT_SDK}
if [ "$1" = "--minimal" ]; then
    shift; ident=${1:-FM-1_9B0}; app=$here/build/minimal/app.bin; out=$here/build/fm1bridge-minimal.fwsc; hint="make -C app MINIMAL=1"
else
    ident=${1:-FM-1_9B1}; app=$here/build/app.bin; out=$here/build/fm1bridge.fwsc; hint="make -C app"
fi
ldr=$FELUCCA/build/loader/ota.bin
[ -f "$app" ] || { echo "package.sh: $app missing: $hint first"; exit 1; }
[ -f "$ldr" ] || { echo "package.sh: $ldr missing: build Felucca once (./build.sh in $FELUCCA) for its loader"; exit 1; }
python3 "$here/tools/fm1pkg_make.py" "$app" "$ldr" "$out" --product "$ident"   # our fork: VM entry ends at 0xE0000
ls -l "$out"
