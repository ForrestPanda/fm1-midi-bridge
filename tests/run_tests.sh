#!/bin/sh
# Host tests: the MIDI code, the update entry, settings, the matrix decoder, the
# advertising parser, the editor and the bridge task on a simulator. Needs a C compiler.
set -e
cd "$(dirname "$0")"
mkdir -p ../build
${CC:-cc} -std=c99 -Wall -Wextra -Wpedantic -Werror -O1 -g -o ../build/test_midi \
    test_midi.c ../app/midi/midi_msg.c ../app/midi/blemidi.c ../app/midi/usbmidi.c ../app/midi/router.c ../app/midi/panic.c ../app/midi/keys.c ../app/midi/decode.c ../app/midi/compose.c ../app/midi/stats.c ../app/midi/recorder.c ../app/midi/merge.c
../build/test_midi
# the update entry as the full build (FM-1_9B1) and as the minimal build (FM-1_9B0), each
# installing full -> minimal -> full from build/ (skipped where a package is missing)
${CC:-cc} -std=c99 -Wall -Wextra -Wpedantic -Werror -O1 -g -I../app -o ../build/test_update \
    test_update.c ../app/update/fm1_update.c ../app/util/crc16.c ../app/util/pack7.c
../build/test_update
${CC:-cc} -std=c99 -Wall -Wextra -Wpedantic -Werror -O1 -g -I../app -DUPD_IDENTITY='"FM-1_9B0"' -o ../build/test_update_minimal \
    test_update.c ../app/update/fm1_update.c ../app/util/crc16.c ../app/util/pack7.c
../build/test_update_minimal
${CC:-cc} -std=c99 -Wall -Wextra -Wpedantic -Werror -O1 -g -I../app -o ../build/test_settings \
    test_settings.c ../app/settings.c ../app/midi/router.c ../app/util/crc16.c
../build/test_settings
${CC:-cc} -std=c99 -Wall -Wextra -Wpedantic -Werror -O1 -g -o ../build/test_matrix test_matrix.c ../app/input/matrix.c
../build/test_matrix
${CC:-cc} -std=c99 -Wall -Wextra -Wpedantic -Werror -O1 -g -o ../build/test_adv_parse test_adv_parse.c ../app/bt_ble/adv_parse.c
../build/test_adv_parse
${CC:-cc} -std=c99 -Wall -Wextra -Wpedantic -Werror -O1 -g -I../app -DFM1_FW_VERSION='"test"' -o ../build/test_editor \
    test_editor.c ../app/editor.c ../app/settings.c ../app/midi/router.c ../app/util/crc16.c ../app/util/pack7.c ../app/util/logring.c
../build/test_editor
# the bridge task on the host simulator (tests/host): SDK headers stand-ins and RAM ports
${CC:-cc} -std=c99 -Wall -Wextra -Wpedantic -Werror -Wno-pointer-to-int-cast -O1 -g -Ihost/sdk -Ihost -I../app -o ../build/test_bridge \
    test_bridge.c host/bridge_sim.c ../app/bridge.c ../app/editor.c ../app/settings.c ../app/util/crc16.c ../app/util/pack7.c ../app/util/logring.c \
    ../app/midi/midi_msg.c ../app/midi/usbmidi.c ../app/midi/blemidi.c ../app/midi/router.c ../app/midi/panic.c ../app/midi/merge.c ../app/midi/stats.c ../app/midi/recorder.c
../build/test_bridge
