#!/bin/sh
# Install build/fm1bridge.fwsc on a connected FM-1 with Felucca's installer
# (needs: pip3 install mido python-rtmidi). --info shows the connected identity.
#   tools/install.sh [--info | fm1_install.py options]
set -e
here=$(cd "$(dirname "$0")/.." && pwd)
FELUCCA=${FELUCCA:-$here/../../Felucca}
if [ "$1" = "--info" ]; then exec python3 "$FELUCCA/tools/fm1_install.py" --info; fi
exec python3 "$FELUCCA/tools/fm1_install.py" "$here/build/fm1bridge.fwsc" "$@"
