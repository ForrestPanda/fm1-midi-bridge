#!/bin/sh
# SPDX-License-Identifier: MIT
# Install a package on a connected FM-1 with Felucca's installer
# (needs: pip3 install mido python-rtmidi). --info shows the connected identity.
#   tools/install.sh [PACKAGE.fwsc] [--info | fm1_install.py options]
#     PACKAGE.fwsc  default build/fm1bridge.fwsc; M-VAVE's FM-1.fwsc goes back to stock
# Env: FELUCCA, the Felucca checkout (default: next to this clone, or one level up)
set -e
here=$(cd "$(dirname "$0")/.." && pwd)
sibling() {   # $1 checked out next to this clone, or one level further up; the first that exists
    for d in "$here/.." "$here/../.."; do [ -d "$d/$1" ] && { (cd "$d/$1" && pwd); return; }; done
    echo "$here/../$1"
}
FELUCCA=${FELUCCA:-$(sibling Felucca)}
[ -f "$FELUCCA/tools/fm1_install.py" ] || { echo "install.sh: no $FELUCCA/tools/fm1_install.py: git clone https://github.com/hugelton/Felucca next to this tree, or set FELUCCA"; exit 1; }
if [ "$1" = "--info" ]; then exec python3 "$FELUCCA/tools/fm1_install.py" --info; fi
pkg=$here/build/fm1bridge.fwsc
case "$1" in *.fwsc) pkg=$1; shift ;; esac
exec python3 "$FELUCCA/tools/fm1_install.py" "$pkg" "$@"
