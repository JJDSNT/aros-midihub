#!/bin/sh
# Writes the pull request N4 of docs/upstream-pr-graph.md into an AROS
# source tree: the service netmidid with AppleMIDI alone, in
# workbench/network/netmidi/netmidid. The sources are this repository's
# own with what the cut leaves out taken off by unifdef: Network MIDI 2.0,
# discovery, the Linux build and the program the host tests run. Only the
# build files and the service file are the cut's.
#
#   tools/cut-netmidi-service.sh AROS_SOURCE_DIR
#
# The tree has to have N1 (the drivers' header) and N3 (the library
# rtpmidi) for the cut to build: tools/cut-netmidi-drivers.sh and
# tools/cut-netmidi-library.sh n3. To take it out again, remove the
# directory it wrote.
set -eu

here=$(cd "$(dirname "$0")/.." && pwd)
tree=$(realpath "$1")
test -f "$tree/workbench/devs/midi/netmidi/netmidi_camd.h"
test -d "$tree/workbench/network/netmidi/rtpmidi/include/rtpmidi"
cut="$tree/workbench/network/netmidi/netmidid"
from="$here/ports/aros/midihub"
rm -rf "$cut"
mkdir -p "$cut/include/netmidi" "$cut/services.d"

# unifdef answers 1 when it took something off, 0 when not: both are well
strip() {
    unifdef -D__AROS__ -DNETMIDI_SERVICE -UNETMIDI_MIDI2 \
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
cp "$here/upstream/n4-service/netmidi.service" "$cut/services.d/netmidi"
# kept under other names here, where mmake would otherwise read them too
cp "$here/upstream/n4-service/mmakefile.src.cut" "$cut/mmakefile.src"
cp "$here/upstream/n4-service/services.d.mmakefile.src.cut" \
   "$cut/services.d/mmakefile.src"
echo "$cut"
