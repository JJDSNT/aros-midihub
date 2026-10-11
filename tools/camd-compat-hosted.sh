#!/bin/sh
# Runs MIDIHubCAMDCompat on Linux-hosted AROS.
#
#   tools/camd-compat-hosted.sh [AROS_DIR [WORK_DIR]]
#
# AROS_DIR is a hosted system tree with Extras/aros-midihub/C built, by
# default build-aros-linux/bin/linux-x86_64/AROS. It is copied to WORK_DIR
# and not modified. Boots the copy twice: once for the 41.1 contract and the
# camd.library 42 and 43 checks, once with DEVS:Midi/debugdriver moved out for
# RethinkCAMD(). Both boots share the copy, so the endpoint IDs of the first
# are compared with those of the second. Needs xvfb-run.
# Exit status: 0 when no check failed.
set -eu

here=$(cd "$(dirname "$0")/.." && pwd)
aros=$(realpath "${1:-$here/build-aros-linux/bin/linux-x86_64/AROS}")
work=$(realpath "${2:-$(mktemp -d)}")
seconds=${CAMDCOMPAT_SECONDS:-120}
sys="$work/AROS"

test -x "$aros/boot/linux/AROSBootstrap"
test -f "$aros/Devs/Midi/debugdriver"
test -f "$aros/Extras/aros-midihub/C/MIDIHubCAMDCompat"

rm -rf "$sys"
mkdir -p "$work"
cp -a --reflink=auto "$aros" "$sys"

boot() {
    printf '%b\nShutdown\n' "$1" > "$sys/S/User-Startup"
    (cd "$sys" && timeout "$seconds" xvfb-run -a boot/linux/AROSBootstrap \
        > "$work/serial-$2.log" 2>&1) || true
    for run in $3; do
        cp "$sys/camdcompat-$run.log" "$work/" 2>/dev/null || true
    done
}

cmd=SYS:Extras/aros-midihub/C/MIDIHubCAMDCompat
boot "$cmd >SYS:camdcompat-contract.log\n$cmd --v42 >SYS:camdcompat-v42.log\n$cmd --v43 >SYS:camdcompat-v43.log" contract "contract v42 v43"

mv "$sys/Devs/Midi/debugdriver" "$sys/camdcompat-debugdriver"
# After the rethink pass a client exits with a session open and memory is
# flushed: camd.library must refuse to go, and then still work.
boot "$cmd --rethink >SYS:camdcompat-rethink.log\n$cmd --v43-leak >SYS:camdcompat-leak.log\nAvail FLUSH >NIL:\n$cmd --v43 >SYS:camdcompat-flushed.log" rethink "rethink leak flushed"

status=0
for run in contract v42 v43 rethink leak flushed; do
    log="$work/camdcompat-$run.log"
    if [ ! -s "$log" ]; then
        echo "no $run log: AROS did not run the suite (see $work/serial-$run.log)"
        status=1
        continue
    fi
    cat "$log"
    grep -q '^FAIL' "$log" && status=1
    tail -n 1 "$log" | grep -q '^camdcompat:' || {
        echo "the $run run did not finish (see $work/serial-$run.log)"
        status=1
    }
done

# The debugdriver's IDs: the second boot must find those the first one made.
ids() {
    sed -n '/camd.library: debugdriver published/,/camd.library: drivers.c/p' \
        "$work/serial-$1.log" | grep -a ' id ' || true
}
first=$(ids contract)
if [ -n "$first" ]; then
    if [ "$first" = "$(ids rethink)" ]; then
        echo "PASS endpoint IDs survive a reboot ($(echo "$first" | wc -l) IDs)"
    else
        echo "FAIL endpoint IDs changed across a reboot"
        status=1
    fi
fi

# The v43 run sends on debugdriver port 2 only: a legacy message, a session's
# two messages and SysEx, a legacy message, and a last session message. The
# driver prints each byte it transmits.
if grep -q '^PASS CloseEndpointSession$' "$work/camdcompat-v43.log" 2>/dev/null; then
    want="92 30 10 91 40 7f 81 40 0 f0 7d 1 2 3 f7 92 31 11 b1 7 64"
    sent=$(grep -a 'Debugdriver has received: .* at port 2' "$work/serial-contract.log" |
        sed 's/.*received: \([0-9a-f]*\) at.*/\1/' | tr '\n' ' ' | sed 's/ $//')
    if [ "$sent" = "$want" ]; then
        echo "PASS the driver transmitted the legacy and session bytes in order"
    else
        echo "FAIL the driver transmitted: $sent"
        status=1
    fi
fi
rm -rf "$sys"
exit $status
