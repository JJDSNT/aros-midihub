#!/bin/sh
# Writes the pull request N4 or N7 of docs/upstream-pr-graph.md into an
# AROS source tree: the service netmidid, in
# workbench/network/netmidi/netmidid. The sources are this repository's
# own with what the cut leaves out taken off by unifdef: discovery, the
# Linux build, the program the host tests run and, for N4, Network MIDI
# 2.0. Only the build files and the service file are the cut's.
#
#   tools/cut-netmidi-service.sh n4|n7 AROS_SOURCE_DIR
#
#   n4   AppleMIDI alone
#   n7   the same with Network MIDI 2.0
#
# The tree has to have N1 (the drivers' header) and N3 (the library
# rtpmidi) for the cut to build, and for n7 also N6 (the library
# netmidi2): tools/cut-netmidi-drivers.sh, tools/cut-netmidi-library.sh n3
# and tools/cut-netmidi2-library.sh. To take it out again, remove the
# directory it wrote.
set -eu

here=$(cd "$(dirname "$0")/.." && pwd)
stage=$1
tree=$(realpath "$2")
case "$stage" in
n4) midi2=-UNETMIDI_MIDI2; build="$here/upstream/n4-service" ;;
n7) midi2=-DNETMIDI_MIDI2; build="$here/upstream/n7-service-midi2"
    test -d "$tree/workbench/network/netmidi/netmidi2/include/netmidi2" ;;
*) echo "n4 or n7" >&2; exit 2 ;;
esac
test -f "$tree/workbench/devs/midi/netmidi/netmidi_camd.h"
test -d "$tree/workbench/network/netmidi/rtpmidi/include/rtpmidi"
cut="$tree/workbench/network/netmidi/netmidid"
from="$here/ports/aros/midihub"
rm -rf "$cut"
mkdir -p "$cut/include/netmidi" "$cut/services.d"

# unifdef answers 1 when it took something off, 0 when not: both are well
strip() {
    unifdef -D__AROS__ -DNETMIDI_SERVICE "$midi2" \
            -UNETMIDI_DISCOVERY "$1" > "$2" || test $? -eq 1
}
strip "$from/main.c" "$cut/main.c"
strip "$from/adapters.c" "$cut/adapters.c"
strip "$from/camd.h" "$cut/camd.h"
strip "$from/network.c" "$cut/network.c"
strip "$from/network.h" "$cut/network.h"
strip "$here/src/config.c" "$cut/config.c"
strip "$here/include/netmidi/config.h" "$cut/include/netmidi/config.h"
cp "$here/ports/aros/netmidi/netmidi_status.h" "$cut/"
cp "$build/netmidi.service" "$cut/services.d/netmidi"
# kept under other names here, where mmake would otherwise read them too
cp "$build/mmakefile.src.cut" "$cut/mmakefile.src"
cp "$here/upstream/n4-service/services.d.mmakefile.src.cut" \
   "$cut/services.d/mmakefile.src"
echo "$cut"
