#!/bin/sh
# Writes the pull request N5 of docs/upstream-pr-graph.md into an AROS
# source tree: C:NetMIDIStatus, in workbench/network/netmidi/netmidistat.
# The source is this repository's own, copied as it is; only the build
# file is the cut's.
#
#   tools/cut-netmidi-status.sh AROS_SOURCE_DIR
#
# The tree has to have N4, whose netmidi_status.h it reads
# (tools/cut-netmidi-service.sh). To take it out again, remove the
# directory it wrote.
set -eu

here=$(cd "$(dirname "$0")/.." && pwd)
tree=$(realpath "$1")
test -f "$tree/workbench/network/netmidi/netmidid/netmidi_status.h"
cut="$tree/workbench/network/netmidi/netmidistat"
rm -rf "$cut"
mkdir -p "$cut"
cp "$here/ports/aros/netmidi/netmidistat.c" "$cut/"
# kept under another name here, where mmake would otherwise read it too
cp "$here/upstream/n5-status/mmakefile.src.cut" "$cut/mmakefile.src"
echo "$cut"
