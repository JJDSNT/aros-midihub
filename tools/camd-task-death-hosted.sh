#!/bin/sh
# Runs the CAMD 43 abnormal-task-termination regression on Linux-hosted AROS.
#
#   tools/camd-task-death-hosted.sh AROS_DIR CAMD_LIBRARY SUITE [WORK_DIR]
#
# The input AROS tree is copied and never modified. CAMD_LIBRARY and SUITE
# must be linux-x86_64 AROS binaries built from the proposal under test.
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
    'SYS:Extras/aros-midihub/C/MIDIHubCAMDCompat --v43-task-death >SYS:camd-task-death.log' \
    'Shutdown' > "$sys/S/camd-task-death-run"
printf '%s\n' 'Run >NIL: Execute S:camd-task-death-run' > "$sys/S/User-Startup"

(cd "$sys" && timeout 90 xvfb-run -a boot/linux/AROSBootstrap \
    > "$work/serial.log" 2>&1) || true

if [ ! -s "$sys/camd-task-death.log" ]; then
    echo "FAIL AROS did not produce camd-task-death.log (see $work/serial.log)"
    exit 1
fi
cp "$sys/camd-task-death.log" "$work/camd-task-death.log"
cat "$work/camd-task-death.log"

status=0
grep -q '^PASS forced termination creates all endpoint object kinds repeatedly$' \
    "$work/camd-task-death.log" || status=1
grep -q "^PASS the private reaper withdraws each dead task's endpoint$" \
    "$work/camd-task-death.log" || status=1
grep -q '^PASS watch, snapshot and signal slots recover after every death$' \
    "$work/camd-task-death.log" || status=1
grep -q '^camdcompat: 3 passed, 0 failed, 0 skipped$' \
    "$work/camd-task-death.log" || status=1

if [ "$status" -ne 0 ]; then
    echo "FAIL abnormal-task-termination regression (see $work)"
    exit "$status"
fi
echo "PASS abnormal-task-termination regression on AROS linux-hosted"
echo "evidence: $work"
