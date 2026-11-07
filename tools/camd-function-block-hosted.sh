#!/bin/sh
# Validates the additive CAMD 43 Function Block record on Linux-hosted AROS.
#
#   tools/camd-function-block-hosted.sh AROS_DIR CAMD_LIBRARY SUITE [WORK_DIR]
set -eu

if [ "$#" -lt 3 ] || [ "$#" -gt 4 ]; then
    echo "usage: $0 AROS_DIR CAMD_LIBRARY SUITE [WORK_DIR]" >&2
    exit 20
fi

aros=$(realpath "$1")
camd=$(realpath "$2")
suite=$(realpath "$3")
work=${4:-$(mktemp -d)}
mkdir -p "$work"
work=$(realpath "$work")
sys="$work/AROS"

test -x "$aros/boot/linux/AROSBootstrap"
test -f "$camd"
test -f "$suite"

rm -rf "$sys"
cp -a --reflink=auto "$aros" "$sys"
cp "$camd" "$sys/Libs/camd.library"
mkdir -p "$sys/Extras/aros-midihub/C"
cp "$suite" "$sys/Extras/aros-midihub/C/MIDIHubCAMDCompat"

printf '%s\n%s\n' \
    'SYS:Extras/aros-midihub/C/MIDIHubCAMDCompat --v43-function-block >SYS:camd-function-block.log' \
    'Shutdown' > "$sys/S/camd-function-block-run"
printf '%s\n' 'Run >NIL: Execute S:camd-function-block-run' > "$sys/S/User-Startup"

(cd "$sys" && timeout 90 xvfb-run -a boot/linux/AROSBootstrap \
    > "$work/serial.log" 2>&1) || true

if [ ! -s "$sys/camd-function-block.log" ]; then
    echo "FAIL AROS did not produce camd-function-block.log (see $work/serial.log)"
    exit 1
fi
cp "$sys/camd-function-block.log" "$work/camd-function-block.log"
cat "$work/camd-function-block.log"

status=0
grep -q '^PASS a 104-byte Function Block publisher remains compatible$' \
    "$work/camd-function-block.log" || status=1
grep -q '^PASS the known Function Block Info Stream vector decodes to 2 and 5$' \
    "$work/camd-function-block.log" || status=1
grep -q '^PASS a 104-byte GetEndpointFunctionBlock caller is not overwritten$' \
    "$work/camd-function-block.log" || status=1
grep -q '^PASS the public API returns MIDI-CI version 2 and five SysEx8 streams$' \
    "$work/camd-function-block.log" || status=1
grep -q '^camdcompat: 8 passed, 0 failed, 0 skipped$' \
    "$work/camd-function-block.log" || status=1

if [ "$status" -ne 0 ]; then
    echo "FAIL Function Block capability regression (see $work)"
    exit "$status"
fi
echo "PASS Function Block capability regression on AROS linux-hosted"
echo "evidence: $work"
