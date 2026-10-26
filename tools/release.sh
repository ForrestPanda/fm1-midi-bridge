#!/bin/sh
# SPDX-License-Identifier: MIT
# The release folder, build/release/: what goes on the GitHub releases page next to a tag.
#   fm1bridge.fwsc, fm1bridge-minimal.fwsc   the packages (tools/package.sh, both builds, first)
#   SHA256SUMS                               over everything in the folder
#   licenses.zip                             LICENSE, THIRD-PARTY-NOTICES.md, LICENSES/: the texts the
#                                            Apache-2.0, GPL-3 and Bitstream Vera licences require
#                                            to travel with the binaries
#   felucca-vX.Y-source.tar.gz               the complete corresponding source of the update loader
#                                            inside each package (GPL-3 section 6): the Felucca tree
#                                            at the tag the loader was built from
# The Felucca checkout must be clean and on a tag, and its built loader must be the one the
# packages carry (the script checks both).
#   tools/release.sh            then: gh release upload TAG build/release/* --clobber
# Env: FELUCCA (default: next to this clone, or one level up)
set -e
here=$(cd "$(dirname "$0")/.." && pwd)
sibling() { for d in "$here/.." "$here/../.."; do [ -d "$d/$1" ] && { (cd "$d/$1" && pwd); return; }; done; echo "$here/../$1"; }
FELUCCA=${FELUCCA:-$(sibling Felucca)}
out=$here/build/release
for p in fm1bridge.fwsc fm1bridge-minimal.fwsc; do
    [ -f "$here/build/$p" ] || { echo "release.sh: build/$p missing: tools/package.sh (and --minimal) first"; exit 1; }
done
tag=$(git -C "$FELUCCA" describe --tags --exact-match 2>/dev/null) || { echo "release.sh: $FELUCCA is not on a tag"; exit 1; }
[ -z "$(git -C "$FELUCCA" status --short)" ] || { echo "release.sh: $FELUCCA has local changes; the loader's source must be the tag"; exit 1; }
ldr=$FELUCCA/build/loader/ota.bin
[ -f "$ldr" ] || { echo "release.sh: $ldr missing: build Felucca once"; exit 1; }
# the loader in each package is the one in the Felucca checkout (ota.bin is stored whole in the UFW container)
for p in fm1bridge.fwsc fm1bridge-minimal.fwsc; do
    python3 - "$here/build/$p" "$ldr" <<'PY' || { echo "release.sh: $p does not carry $ldr"; exit 1; }
import sys
pkg, ldr = open(sys.argv[1], 'rb').read(), open(sys.argv[2], 'rb').read()
sys.exit(0 if ldr in pkg else 1)
PY
done
rm -rf "$out"; mkdir -p "$out"
cp "$here/build/fm1bridge.fwsc" "$here/build/fm1bridge-minimal.fwsc" "$out/"
(cd "$here" && rm -f "$out/licenses.zip" && zip -q -r "$out/licenses.zip" LICENSE THIRD-PARTY-NOTICES.md LICENSES)
git -C "$FELUCCA" archive --format=tar --prefix="felucca-$tag/" "$tag" | gzip -9 > "$out/felucca-$tag-source.tar.gz"
(cd "$out" && sha256sum fm1bridge.fwsc fm1bridge-minimal.fwsc licenses.zip "felucca-$tag-source.tar.gz" > SHA256SUMS)
echo "loader: $tag $(git -C "$FELUCCA" rev-parse HEAD), $(sha256sum "$ldr" | cut -c1-64)"
ls -l "$out"; cat "$out/SHA256SUMS"
echo "next: gh release upload TAG $out/* --clobber"
