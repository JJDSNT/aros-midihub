#!/bin/sh
# Run the DNS-SD D1 ownership prototype on Linux-hosted AROS.
#
#   tools/dnssd-coordinator-hosted.sh AROS_DIR PROBE [WORK_DIR]
set -eu

if [ "$#" -lt 2 ] || [ "$#" -gt 3 ]; then
    echo "usage: $0 AROS_DIR PROBE [WORK_DIR]" >&2
    exit 20
fi

aros=$(realpath "$1")
probe=$(realpath "$2")
work=${3:-$(mktemp -d)}
mkdir -p "$work"
work=$(realpath "$work")
sys="$work/AROS"

test -x "$aros/boot/linux/AROSBootstrap"
test -f "$probe"

rm -rf "$sys"
cp -a --reflink=auto "$aros" "$sys"
mkdir -p "$sys/Extras/aros-midihub/Tests"
cp "$probe" "$sys/Extras/aros-midihub/Tests/DNSSDCoordProbe"

printf '%s\n%s\n' \
    'SYS:Extras/aros-midihub/Tests/DNSSDCoordProbe >SYS:dnssd-coordinator.log' \
    'Shutdown' > "$sys/S/dnssd-coordinator-run"
printf '%s\n' 'Run >NIL: Execute S:dnssd-coordinator-run' \
    > "$sys/S/User-Startup"

(cd "$sys" && timeout 90 xvfb-run -a boot/linux/AROSBootstrap \
    > "$work/serial.log" 2>&1) || true

if [ ! -s "$sys/dnssd-coordinator.log" ]; then
    echo "FAIL AROS did not produce dnssd-coordinator.log (see $work/serial.log)"
    exit 1
fi
cp "$sys/dnssd-coordinator.log" "$work/dnssd-coordinator.log"
cat "$work/dnssd-coordinator.log"

status=0
grep -q '^PASS conflict is exclusive while an independent transaction proceeds$' \
    "$work/dnssd-coordinator.log" || status=1
grep -q '^PASS forced task removal reclaims operation and stale notification$' \
    "$work/dnssd-coordinator.log" || status=1
grep -q '^PASS old operation token is stale after restart$' \
    "$work/dnssd-coordinator.log" || status=1
grep -q '^PASS fresh session operates after restart$' \
    "$work/dnssd-coordinator.log" || status=1
grep -q '^DNSSD-COORDINATOR 16 checks, 0 failures$' \
    "$work/dnssd-coordinator.log" || status=1

if [ "$status" -ne 0 ]; then
    echo "FAIL DNS-SD coordinator prototype (see $work)"
    exit "$status"
fi
echo "PASS DNS-SD coordinator prototype on AROS linux-hosted"
echo "evidence: $work"
