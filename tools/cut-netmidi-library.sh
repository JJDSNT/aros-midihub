#!/bin/sh
# Writes the pull requests N2 and N3 of docs/upstream-pr-graph.md into an
# AROS source tree: the RTP-MIDI and AppleMIDI code as the link library
# rtpmidi, in workbench/network/netmidid/rtpmidi. The sources are this
# repository's own, copied as they are; only the build file is the cut's.
#
#   tools/cut-netmidi-library.sh n2|n3 AROS_SOURCE_DIR
#
#   n2   the payload: command section, journal, sender, receiver, timing
#   n3   the same with the sessions: AppleMIDI packets, peers, ports, clock
#
# Nothing else of this repository has to be in that tree for the cut to
# build. To take it out again, remove the rtpmidi directory it wrote.
set -eu

here=$(cd "$(dirname "$0")/.." && pwd)
stage=$1
tree=$(realpath "$2")
test -d "$tree/workbench/network"
cut="$tree/workbench/network/netmidid/rtpmidi"
payload="rtpmidi sender receiver timing"
sessions="applemidi session peers ports clock"
case "$stage" in
n2) parts="$payload"; build="$here/upstream/n2-library/mmakefile.src.cut" ;;
n3) parts="$payload $sessions"; build="$here/upstream/n3-sessions/mmakefile.src.cut" ;;
*) echo "n2 or n3" >&2; exit 2 ;;
esac
rm -rf "$cut"
mkdir -p "$cut/include/rtpmidi"
for part in $parts; do
    cp "$here/network/rtpmidi/$part.c" "$cut/"
    cp "$here/include/rtpmidi/$part.h" "$cut/include/rtpmidi/"
done
# kept under another name here, where mmake would otherwise read it too
cp "$build" "$cut/mmakefile.src"
echo "$cut"
