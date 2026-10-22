#!/bin/sh
# Writes the pull request N1 of docs/upstream-pr-graph.md into an AROS
# source tree: the two CAMD drivers for MIDI over the network, as
# workbench/devs/camd. The sources are this repository's own,
# copied as they are; only the build files are the cut's.
#
#   tools/cut-netmidi-drivers.sh AROS_SOURCE_DIR
#
# First apply the CAMD subsystem move from PR #1586 (debugdriver and
# hostmidi). Nothing else of this repository is needed in that tree.
# To take the cut out, remove applemidi and networkmidi2, and
# their parent metatarget entries.
# The existing debugdriver remains part of the CAMD subsystem.
set -eu

here=$(cd "$(dirname "$0")/.." && pwd)
tree=$(realpath "$1")
test -f "$tree/workbench/devs/camd/debugdriver/mmakefile.src"
cut="$tree/workbench/devs/camd"
mkdir -p "$cut/applemidi" "$cut/networkmidi2"
cp "$here/ports/aros/netmidi/netmididriver.c" \
   "$here/ports/aros/netmidi/netmidi_camd.h" "$cut/networkmidi2/"
# kept under another name here, where mmake would otherwise read it too
cp "$here/upstream/n1-drivers/mmakefile.src.cut" "$cut/mmakefile.src"
for driver in applemidi networkmidi2; do
    cp "$here/upstream/n1-drivers/$driver.mmakefile.src.cut" \
       "$cut/$driver/mmakefile.src"
done
echo "$cut"
