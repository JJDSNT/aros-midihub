#!/bin/sh
# Writes the pull request N4, N7, N8 or N9 of docs/upstream-pr-graph.md
# into an AROS source tree: the service netmidid, in
# workbench/network/netmidi/netmidid. The sources are this repository's
# own with what the cut leaves out taken off by unifdef: the Linux build,
# the program the host tests run and what later pull requests add. Only
# the build files and the service file are the cut's.
#
#   tools/cut-netmidi-service.sh n4|n7|n8|n9 AROS_SOURCE_DIR
#
#   n4   AppleMIDI alone
#   n7   the same with Network MIDI 2.0
#   n8   the same advertised through the system's mDNS service
#   n9   the same with peers looked for by name
#
# n8 and n9 need bonami.library's headers in the tree, which is M2 and M3
# of the graph.
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
n4) marks="-UNETMIDI_MIDI2 -UNETMIDI_DISCOVERY -UNETMIDI_BROWSE"
    build="$here/upstream/n4-service"; service=$build ;;
n7) marks="-DNETMIDI_MIDI2 -UNETMIDI_DISCOVERY -UNETMIDI_BROWSE"
    build="$here/upstream/n7-service-midi2"; service=$build ;;
n8) marks="-DNETMIDI_MIDI2 -DNETMIDI_DISCOVERY -UNETMIDI_BROWSE"
    build="$here/upstream/n8-discovery"; service=$build ;;
n9) marks="-DNETMIDI_MIDI2 -DNETMIDI_DISCOVERY -DNETMIDI_BROWSE"
    build="$here/upstream/n8-discovery"
    service="$here/upstream/n9-browse" ;;
*) echo "n4, n7, n8 or n9" >&2; exit 2 ;;
esac
test "$stage" = n4 ||
    test -d "$tree/workbench/network/netmidi/netmidi2/include/netmidi2"
test -f "$tree/workbench/devs/midi/netmidi/netmidi_camd.h"
test -d "$tree/workbench/network/netmidi/rtpmidi/include/rtpmidi"
cut="$tree/workbench/network/netmidi/netmidid"
from="$here/ports/aros/midihub"
rm -rf "$cut"
mkdir -p "$cut/include/netmidi" "$cut/services.d"

# unifdef answers 1 when it took something off, 0 when not: both are well
strip() {
    # shellcheck disable=SC2086
    unifdef -D__AROS__ -DNETMIDI_SERVICE $marks "$1" > "$2" || test $? -eq 1
}
strip "$from/main.c" "$cut/main.c"
strip "$from/adapters.c" "$cut/adapters.c"
strip "$from/camd.h" "$cut/camd.h"
strip "$from/network.c" "$cut/network.c"
strip "$from/network.h" "$cut/network.h"
strip "$here/src/config.c" "$cut/config.c"
strip "$here/include/netmidi/config.h" "$cut/include/netmidi/config.h"
case "$stage" in n8|n9)
    strip "$from/discovery.c" "$cut/discovery.c"
    strip "$from/discovery.h" "$cut/discovery.h" ;;
esac
cp "$here/ports/aros/netmidi/netmidi_status.h" "$cut/"
cp "$service/netmidi.service" "$cut/services.d/netmidi"
# kept under other names here, where mmake would otherwise read them too
cp "$build/mmakefile.src.cut" "$cut/mmakefile.src"
cp "$here/upstream/n4-service/services.d.mmakefile.src.cut" \
   "$cut/services.d/mmakefile.src"
echo "$cut"
