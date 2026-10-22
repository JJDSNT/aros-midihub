#!/bin/sh
# Writes the pull request N4, N7, N8 or N9 of docs/upstream-pr-graph.md
# into an AROS source tree: the service netmidid, in
# workbench/network/netmidid. The sources are this repository's
# own with what the cut leaves out taken off by unifdef: the Linux build,
# the program the host tests run and what later pull requests add. Only
# the build files and the service file are the cut's.
#
#   tools/cut-netmidi-service.sh n4|n7|n8-applemidi|n8|n9 AROS_SOURCE_DIR
#
#   n4   AppleMIDI alone
#   n7   the same with Network MIDI 2.0
#   n8-applemidi  AppleMIDI advertised, without the MIDI 2.0 dependency
#   n8   the same advertised through the system's mDNS service
#   n9   the same with peers looked for by name
#
# n8 and n9 need bonami.library's headers in the tree, which is M2 and M3
# of the graph.
# The tree has to have N1 (the drivers' header) and N3 (the library
# rtpmidi) for the cut to build, and for n7 also N6 (the library
# netmidi2): tools/cut-netmidi-drivers.sh, tools/cut-netmidi-library.sh n3
# and tools/cut-netmidi2-library.sh. To take it out again, remove the
# service files it wrote; keep the protocol subdirectories.
set -eu

here=$(cd "$(dirname "$0")/.." && pwd)
stage=$1
tree=$(realpath "$2")
case "$stage" in
n4) marks="-DNETMIDI_APPLEMIDI -UNETMIDI_MIDI2 -UNETMIDI_DISCOVERY -UNETMIDI_BROWSE"
    build="$here/upstream/n4-service"; service=$build ;;
n7) marks="-DNETMIDI_APPLEMIDI -DNETMIDI_MIDI2 -UNETMIDI_DISCOVERY -UNETMIDI_BROWSE"
    build="$here/upstream/n7-service-midi2"; service=$build ;;
n8-applemidi) marks="-DNETMIDI_APPLEMIDI -UNETMIDI_MIDI2 -DNETMIDI_DISCOVERY -UNETMIDI_BROWSE"
    build="$here/upstream/n8-applemidi"; service=$build ;;
n8) marks="-DNETMIDI_APPLEMIDI -DNETMIDI_MIDI2 -DNETMIDI_DISCOVERY -UNETMIDI_BROWSE"
    build="$here/upstream/n8-discovery"; service=$build ;;
n9) marks="-DNETMIDI_APPLEMIDI -DNETMIDI_MIDI2 -DNETMIDI_DISCOVERY -DNETMIDI_BROWSE"
    build="$here/upstream/n8-discovery"
    service="$here/upstream/n9-browse" ;;
*) echo "n4, n7, n8-applemidi, n8 or n9" >&2; exit 2 ;;
esac
test "$stage" = n4 || test "$stage" = n8-applemidi ||
    test -d "$tree/workbench/network/netmidid/netmidi2/include/netmidi2"
test -f "$tree/workbench/devs/camd/networkmidi2/netmidi_camd.h"
test -d "$tree/workbench/network/netmidid/rtpmidi/include/rtpmidi"
cut="$tree/workbench/network/netmidid"
from="$here/ports/aros/midihub"
# Protocol libraries live below the service now; do not remove them.
rm -f "$cut/discovery.c" "$cut/discovery.h" "$cut/transport_midi2.c"
mkdir -p "$cut/include/netmidi" "$cut/services.d"

# unifdef answers 1 when it took something off, 0 when not: both are well
strip() {
    # shellcheck disable=SC2086
    unifdef -D__AROS__ -DNETMIDI_SERVICE $marks "$1" > "$2" || test $? -eq 1
}
strip "$from/main.c" "$cut/main.c"
strip "$from/netmidid.h" "$cut/netmidid.h"
strip "$from/transport_applemidi.c" "$cut/transport_applemidi.c"
strip "$from/adapters.c" "$cut/adapters.c"
strip "$from/camd.h" "$cut/camd.h"
strip "$from/network.c" "$cut/network.c"
strip "$from/network.h" "$cut/network.h"
strip "$here/src/config.c" "$cut/config.c"
strip "$here/include/netmidi/config.h" "$cut/include/netmidi/config.h"
case "$stage" in n7|n8|n9)
    strip "$from/transport_midi2.c" "$cut/transport_midi2.c" ;;
esac
case "$stage" in n8-applemidi|n8|n9)
    strip "$from/discovery.c" "$cut/discovery.c"
    strip "$from/discovery.h" "$cut/discovery.h" ;;
esac
cp "$here/ports/aros/netmidi/netmidi_status.h" "$cut/"
cp "$service/netmidi.service" "$cut/services.d/netmidi"
# kept under other names here, where mmake would otherwise read them too
cp "$build/mmakefile.src.cut" "$cut/mmakefile.src"
cp "$here/upstream/n4-service/services.d.mmakefile.src.cut" \
   "$cut/services.d/mmakefile.src"
if [ "$stage" = n8-applemidi ]; then
    cp "$build/ADVERTISEMENT.md" "$cut/ADVERTISEMENT.md"
fi
echo "$cut"
