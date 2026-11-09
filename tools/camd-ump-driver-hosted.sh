#!/bin/sh
# Runs the native UMP resident-driver reference probe on Linux-hosted AROS.
#
#   tools/camd-ump-driver-hosted.sh AROS_DIR CAMD_LIBRARY PROBE [WORK_DIR]
set -eu

if [ "$#" -lt 3 ] || [ "$#" -gt 4 ]; then
    echo "usage: $0 AROS_DIR CAMD_LIBRARY PROBE [WORK_DIR]" >&2
    exit 20
fi

aros=$(realpath "$1")
camd=$(realpath "$2")
probe=$(realpath "$3")
if [ "$#" -eq 4 ]; then
    work=$4
    if [ -e "$work" ]; then
        echo "refusing existing work directory: $work" >&2
        exit 20
    fi
    mkdir -p "$work"
else
    work=$(mktemp -d)
fi
work=$(realpath "$work")
sys="$work/AROS"
display=${CAMD_UMP_X_DISPLAY:-:88}
xvfb_pid=

test -x "$aros/boot/linux/AROSBootstrap"
test -f "$camd"
test -f "$probe"
cp -a --reflink=auto "$aros" "$sys"
cp "$camd" "$sys/Libs/camd.library"
mkdir -p "$sys/Extras/aros-midihub/C"
cp "$probe" "$sys/Extras/aros-midihub/C/MIDIHubUMPDriverProbe"
printf '%s\n%s\n' \
    'SYS:Extras/aros-midihub/C/MIDIHubUMPDriverProbe >SYS:camd-ump-driver.log' \
    'Shutdown' > "$sys/S/camd-ump-driver-run"
printf '%s\n' 'Run >NIL: Execute S:camd-ump-driver-run' > "$sys/S/User-Startup"

cleanup()
{
    if [ -n "$xvfb_pid" ]; then kill "$xvfb_pid" 2>/dev/null || true; fi
}
trap cleanup EXIT INT TERM

Xvfb "$display" -ac -screen 0 1024x768x24 -nolisten unix -listen tcp \
    > "$work/xvfb.log" 2>&1 &
xvfb_pid=$!
sleep 1
(cd "$sys" && DISPLAY=127.0.0.1$display timeout -k 2 90 boot/linux/AROSBootstrap \
    > "$work/serial.log" 2>&1) || true
if [ ! -s "$sys/camd-ump-driver.log" ]; then
    echo "FAIL AROS did not produce camd-ump-driver.log (see $work/serial.log)"
    exit 1
fi
cp "$sys/camd-ump-driver.log" "$work/camd-ump-driver.log"
cat "$work/camd-ump-driver.log"
status=0
grep -q '^PASS AddMidiDevice accepts the adjacent native-UMP extension$' \
    "$work/camd-ump-driver.log" || status=1
grep -q '^PASS the resident driver publishes a native MIDI 2.0 UMP endpoint$' \
    "$work/camd-ump-driver.log" || status=1
grep -q '^PASS the reference driver returns the same four UMP words$' \
    "$work/camd-ump-driver.log" || status=1
grep -q '^PASS removal is refused while endpoint sessions pin the driver$' \
    "$work/camd-ump-driver.log" || status=1
grep -q '^ump-driver-probe: 0 failure(s)$' "$work/camd-ump-driver.log" || status=1
if [ "$status" -ne 0 ]; then
    echo "FAIL native UMP driver regression (see $work)"
    exit "$status"
fi
echo "PASS native UMP driver regression on AROS linux-hosted"
echo "evidence: $work"
