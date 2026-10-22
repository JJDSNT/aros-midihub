#!/bin/sh
# Writes the pull request N6 of docs/upstream-pr-graph.md into an AROS
# source tree: the Universal MIDI Packet and Network MIDI 2.0 code as the
# link library netmidi2, in workbench/network/netmidid/netmidi2. The
# sources are this repository's own, copied as they are; only the build
# file is the cut's.
#
#   tools/cut-netmidi2-library.sh AROS_SOURCE_DIR
#
# Nothing else of this repository has to be in that tree for the cut to
# build. To take it out again, remove the directory it wrote.
set -eu

here=$(cd "$(dirname "$0")/.." && pwd)
tree=$(realpath "$1")
test -d "$tree/workbench/network"
cut="$tree/workbench/network/netmidid/netmidi2"
rm -rf "$cut"
mkdir -p "$cut/include/ump" "$cut/include/netmidi2"
cp "$here/src/ump.c" "$here/src/netmidi2.c" "$cut/"
cp "$here/include/ump/ump.h" "$cut/include/ump/"
cp "$here/include/netmidi2/netmidi2.h" "$cut/include/netmidi2/"
# kept under another name here, where mmake would otherwise read it too
cp "$here/upstream/n6-midi2/mmakefile.src.cut" "$cut/mmakefile.src"
echo "$cut"
