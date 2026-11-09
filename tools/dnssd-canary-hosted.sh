#!/bin/sh
# Run a native DNS-SD architecture canary on Linux-hosted AROS.
#
#   tools/dnssd-canary-hosted.sh AROS_DIR PROBE model|link [WORK_DIR]
set -eu

if [ "$#" -lt 3 ] || [ "$#" -gt 4 ]; then
    echo "usage: $0 AROS_DIR PROBE model|link [WORK_DIR]" >&2
    exit 20
fi

aros=$(realpath "$1")
probe=$(realpath "$2")
kind=$3
work=${4:-$(mktemp -d)}
mkdir -p "$work"
work=$(realpath "$work")
sys="$work/AROS"

case "$kind" in
    model)
        test_name=dnssdmodeltest
        expected='DNS-SD canonical model: 0 failures'
        ;;
    link)
        test_name=dnssdlinktest
        expected='DNS-SD link inventory: 0 failures'
        ;;
    *)
        echo "unknown canary '$kind' (expected model or link)" >&2
        exit 20
        ;;
esac

test -x "$aros/boot/linux/AROSBootstrap"
test -f "$probe"

rm -rf "$sys"
cp -a --reflink=auto "$aros" "$sys"
mkdir -p "$sys/Tests"
cp "$probe" "$sys/Tests/$test_name"

printf '%s\n%s\n' \
    "SYS:Tests/$test_name >SYS:dnssd-canary.log" \
    'Shutdown' > "$sys/S/dnssd-canary-run"
printf '%s\n' 'Run >NIL: Execute S:dnssd-canary-run' \
    > "$sys/S/User-Startup"

(cd "$sys" && timeout 90 xvfb-run -a boot/linux/AROSBootstrap \
    > "$work/serial.log" 2>&1) || true

if [ ! -s "$sys/dnssd-canary.log" ]; then
    echo "FAIL AROS did not produce dnssd-canary.log (see $work/serial.log)"
    exit 1
fi
cp "$sys/dnssd-canary.log" "$work/dnssd-canary.log"
cat "$work/dnssd-canary.log"

if ! grep -Fqx "$expected" "$work/dnssd-canary.log"; then
    echo "FAIL DNS-SD $kind canary (see $work)"
    exit 1
fi

echo "PASS DNS-SD $kind canary on AROS linux-hosted"
echo "evidence: $work"
