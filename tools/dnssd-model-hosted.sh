#!/bin/sh
# Run the canonical DNS-SD model canary on Linux-hosted AROS.
#
#   tools/dnssd-model-hosted.sh AROS_DIR PROBE [WORK_DIR]
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
mkdir -p "$sys/Tests"
cp "$probe" "$sys/Tests/dnssdmodeltest"

printf '%s\n%s\n' \
    'SYS:Tests/dnssdmodeltest >SYS:dnssd-model.log' \
    'Shutdown' > "$sys/S/dnssd-model-run"
printf '%s\n' 'Run >NIL: Execute S:dnssd-model-run' \
    > "$sys/S/User-Startup"

(cd "$sys" && timeout 90 xvfb-run -a boot/linux/AROSBootstrap \
    > "$work/serial.log" 2>&1) || true

if [ ! -s "$sys/dnssd-model.log" ]; then
    echo "FAIL AROS did not produce dnssd-model.log (see $work/serial.log)"
    exit 1
fi
cp "$sys/dnssd-model.log" "$work/dnssd-model.log"
cat "$work/dnssd-model.log"

if ! grep -q '^DNS-SD canonical model: 0 failures$' \
    "$work/dnssd-model.log"; then
    echo "FAIL DNS-SD canonical model canary (see $work)"
    exit 1
fi

echo "PASS DNS-SD canonical model on AROS linux-hosted"
echo "evidence: $work"
