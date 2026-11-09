#!/bin/sh
# Run a native DNS-SD architecture canary on Linux-hosted AROS.
#
#   tools/dnssd-canary-hosted.sh AROS_DIR PROBE model|link|scan [WORK_DIR]
set -eu

if [ "$#" -lt 3 ] || [ "$#" -gt 4 ]; then
    echo "usage: $0 AROS_DIR PROBE model|link|scan [WORK_DIR]" >&2
    exit 20
fi

aros=$(realpath "$1")
probe=$(realpath "$2")
kind=$3
needs_network=0
cleanup_if=

cleanup()
{
    if [ -n "$cleanup_if" ]; then
        sudo ip link del "$cleanup_if" >/dev/null 2>&1 || true
    fi
}
trap cleanup EXIT
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
    scan)
        test_name=dnssdlinkarostest
        expected='DNS-SD AROSTCP link collector: 0 failures'
        needs_network=1
        ;;
    *)
        echo "unknown canary '$kind' (expected model, link or scan)" >&2
        exit 20
        ;;
esac

test -x "$aros/boot/linux/AROSBootstrap"
test -f "$probe"

rm -rf "$sys"
cp -a --reflink=auto "$aros" "$sys"
if [ "$needs_network" -eq 1 ]; then
    aros_if=eth9
    peer_if=dnssdhost9
    if ip link show "$aros_if" >/dev/null 2>&1 ||
       ip link show "$peer_if" >/dev/null 2>&1; then
        echo "FAIL test interfaces $aros_if or $peer_if already exist" >&2
        exit 1
    fi
    sudo ip link add "$aros_if" type veth peer name "$peer_if"
    cleanup_if=$peer_if
    sudo ip link set "$aros_if" up
    sudo ip link set "$peer_if" up
    sudo sysctl -qw "net.ipv6.conf.$aros_if.disable_ipv6=1"
    sudo setcap cap_net_raw,cap_net_admin+ep "$sys/boot/linux/AROSBootstrap"
    db="$sys/System/Network/AROSTCP/db"
    printf '%s\n' \
        "$aros_if DEV=DEVS:networks/eth.device UNIT=9 IP=192.0.2.2 NETMASK=255.255.255.0 IP6=fe80::2 PREFIXLEN=64 UP" \
        >> "$db/interfaces"
    sed -i 's/OPENGUI=.*/OPENGUI=NO/' "$db/general.config"
fi
mkdir -p "$sys/Tests"
cp "$probe" "$sys/Tests/$test_name"

if [ "$needs_network" -eq 1 ]; then
    printf '%s\n' \
        'FailAt 21' \
        'CD SYS:System/Network/AROSTCP' \
        'Path C S ADD QUIET' \
        'Run >NIL: QUIET AROSTCP' \
        'WaitForPort AROSTCP' \
        'Wait 3' \
        "SYS:Tests/$test_name >SYS:dnssd-canary.log" \
        'Shutdown' > "$sys/S/dnssd-canary-run"
else
    printf '%s\n%s\n' \
        "SYS:Tests/$test_name >SYS:dnssd-canary.log" \
        'Shutdown' > "$sys/S/dnssd-canary-run"
fi
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
