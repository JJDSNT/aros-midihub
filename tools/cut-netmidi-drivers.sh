#!/bin/sh
# Writes the pull request N1 of docs/upstream-pr-graph.md into an AROS
# source tree: the two CAMD drivers for MIDI over the network, as
# workbench/devs/midi/netmidi. The sources are this repository's own,
# copied as they are; only the build file is the cut's.
#
#   tools/cut-netmidi-drivers.sh AROS_SOURCE_DIR
#
# Nothing else of this repository has to be in that tree for the cut to
# build. To take it out again, remove the directory it wrote.
set -eu

here=$(cd "$(dirname "$0")/.." && pwd)
tree=$(realpath "$1")
test -f "$tree/workbench/devs/midi/mmakefile.src"
cut="$tree/workbench/devs/midi/netmidi"
mkdir -p "$cut"
cp "$here/ports/aros/netmidi/netmididriver.c" \
   "$here/ports/aros/netmidi/netmidi_camd.h" "$cut/"
# kept under another name here, where mmake would otherwise read it too
cp "$here/upstream/n1-drivers/mmakefile.src.cut" "$cut/mmakefile.src"
echo "$cut"
