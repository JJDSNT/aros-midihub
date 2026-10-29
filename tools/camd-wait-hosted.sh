#!/bin/sh
# Isolates the legacy CamdWait regression on Linux-hosted AROS.
#
#   tools/camd-wait-hosted.sh LABEL [CAMD_LIBRARY]
#
# CAMD_LIBRARY defaults to the library in the base image. The copied image is
# left in /tmp/camd-wait-LABEL with its serial log and any completed test log.
set -eu

here=$(cd "$(dirname "$0")/.." && pwd)
label=$1
library=${2:--}
base=/home/jaime/aros-build-ipv6-hosted/bin/linux-x86_64/AROS
work=/tmp/camd-wait-$label
sys=$work/AROS
display=:78
xvfb_pid=

if [ -e "$work" ]; then
    echo "refusing existing work directory: $work" >&2
    exit 20
fi
test -x "$base/boot/linux/AROSBootstrap"
test -f "$here/build-aros-linux/bin/linux-x86_64/AROS/Extras/aros-midihub/C/MIDIHubCAMDCompat"
test -f "$here/build-aros-linux/bin/linux-x86_64/AROS/Extras/aros-midihub/Tests/waitdriver"
if [ "$library" != - ]; then test -f "$library"; fi

mkdir -p "$work"
cp -a --reflink=auto "$base" "$sys"
mkdir -p "$sys/Extras/aros-midihub/C" "$sys/Devs/Midi"
cp "$here/build-aros-linux/bin/linux-x86_64/AROS/Extras/aros-midihub/C/MIDIHubCAMDCompat" \
    "$sys/Extras/aros-midihub/C/MIDIHubCAMDCompat"
cp "$here/build-aros-linux/bin/linux-x86_64/AROS/Extras/aros-midihub/Tests/waitdriver" \
    "$sys/Devs/Midi/waitdriver"
if [ "$library" != - ]; then cp "$library" "$sys/Libs/camd.library"; fi

printf '%s\n' \
    'SYS:Extras/aros-midihub/C/MIDIHubCAMDCompat >SYS:camd-contract.log' \
    'SYS:Extras/aros-midihub/C/MIDIHubCAMDCompat --legacy-wait >SYS:camd-wait.log' \
    'Shutdown' > "$sys/S/camd-wait-run"
printf '%s\n' 'Run >NIL: Execute S:camd-wait-run' > "$sys/S/User-Startup"

cleanup()
{
    if [ -n "$xvfb_pid" ]; then kill "$xvfb_pid" 2>/dev/null || true; fi
}
trap cleanup EXIT INT TERM

Xvfb "$display" -ac -screen 0 1024x768x24 -nolisten unix -listen tcp \
    >"$work/xvfb.log" 2>&1 &
xvfb_pid=$!
sleep 1
(cd "$sys" && DISPLAY=127.0.0.1$display timeout -k 2 30 boot/linux/AROSBootstrap \
    >"$work/serial.log" 2>&1) || true

if [ -s "$sys/camd-contract.log" ]; then
    cp "$sys/camd-contract.log" "$work/camd-contract.log"
    cat "$work/camd-contract.log"
fi
if ! tail -n 1 "$work/camd-contract.log" 2>/dev/null | grep -q '^camdcompat:' ||
   grep -q '^FAIL' "$work/camd-contract.log"; then
    echo "FAIL: the CAMD 41.1 contract did not pass"
    echo "ARTIFACTS $work"
    exit 5
fi
if [ -s "$sys/camd-wait.log" ]; then
    cp "$sys/camd-wait.log" "$work/camd-wait.log"
    cat "$work/camd-wait.log"
fi
if ! tail -n 1 "$work/camd-wait.log" 2>/dev/null | grep -q '^camdcompat:'; then
    echo "INCOMPLETE: the legacy wait run did not finish"
    echo "ARTIFACTS $work"
    exit 5
fi
if grep -q '^FAIL' "$work/camd-wait.log"; then
    echo "ARTIFACTS $work"
    exit 5
fi
echo "ARTIFACTS $work"
