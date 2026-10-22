#!/bin/sh
# Export the transport-neutral helpers used by netmidid and its protocols.
# Usage: tools/cut-netmidi-common.sh AROS_SOURCE_DIR
set -eu
here=$(cd "$(dirname "$0")/.." && pwd)
tree=$(realpath "$1")
test -d "$tree/workbench/network"
cut="$tree/workbench/network/netmidid/common"
mkdir -p "$cut/include/netmidi"
for part in ports text queue; do
    cp "$here/network/netmidi/$part.c" "$cut/"
    cp "$here/include/netmidi/$part.h" "$cut/include/netmidi/"
done
cp "$here/include/netmidi/limits.h" "$cut/include/netmidi/"
cp "$here/upstream/netmidi-common/mmakefile.src.cut" "$cut/mmakefile.src"
echo "$cut"
