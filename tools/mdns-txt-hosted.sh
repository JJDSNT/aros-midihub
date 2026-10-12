#!/bin/sh
# Checks on Linux-hosted AROS that a service's text record keeps a value
# with spaces in it, both ways: what a client on AROS registers, as the
# host's Avahi reads it, and what the host registers, as a client on AROS
# is given it. Network MIDI 2.0 advertises its endpoint's name so, and a
# name has spaces (M2-124-UM section 4.4).
#
#   tools/mdns-txt-hosted.sh [AROS_DIR [WORK_DIR]]
#
# eth1 is AROS's (192.168.166.2), aroshost the host's (192.168.166.1). It
# needs passwordless sudo, a running avahi-daemon, avahi-utils, ethtool and
# xvfb-run.
#
# AROS registers "Txt Test" with four items, two of them with spaces and
# one with a backslash, and resolves "Host Txt", which the host registers
# with one item that has spaces and one that has none.
# Exit status: 0 when both records came through as written.
set -eu

here=$(cd "$(dirname "$0")/.." && pwd)
aros=$(realpath "${1:-$here/build-aros-linux/bin/linux-x86_64/AROS}")
work=$(realpath "${2:-$(mktemp -d)}")
sys="$work/AROS"
c=SYS:Extras/aros-midihub/C
type=_txttest._udp

test -x "$aros/boot/linux/AROSBootstrap"
test -f "$aros/Extras/aros-midihub/C/dnssd"
test -f "$aros/System/Network/Services/mdnsd"

if ! ip link show aroshost > /dev/null 2>&1; then
    sudo ip link add eth1 type veth peer name aroshost
    sudo ip addr add 192.168.166.1/24 dev aroshost
fi
sudo ip link set eth1 up
sudo ip link set aroshost up
sudo sysctl -qw net.ipv6.conf.eth1.disable_ipv6=1
sudo ethtool -K aroshost tx off > /dev/null
sudo ethtool -K eth1 tx off > /dev/null

sudo rm -rf "$sys"
mkdir -p "$work"
cp -a --reflink=auto "$aros" "$sys"
db="$sys/System/Network/AROSTCP/db"
echo 'eth1 DEV=DEVS:networks/eth.device UNIT=1 IP=192.168.166.2 NETMASK=255.255.255.0 UP' \
    >> "$db/interfaces"
printf 'HOST 192.168.166.2 arosbox.arosnet arosbox\nDOMAIN arosnet 192.168.166.\n' \
    > "$db/netdb-myhost"
sed -i 's/OPENGUI=.*/OPENGUI=NO/' "$db/general.config"
sudo setcap cap_net_raw,cap_net_admin+ep "$sys/boot/linux/AROSBootstrap"
cat > "$sys/S/txt-run" <<RUNEOF
FailAt 21
CD SYS:System/Network/AROSTCP
Path C S ADD QUIET
Run >NIL: QUIET AROSTCP
WaitForPort AROSTCP
Wait 8
Run >SYS:txt-register.log $c/dnssd -R "Txt Test" $type . 5000 "UMPEndpointName=AROS MIDI Hub" "ProductInstanceId=abc-1" "note=two  spaces" "path=C:\\Dir"
Run >SYS:txt-browse.log $c/dnssd -Z $type
Wait 8
Run >SYS:txt-query.log $c/dnssd -q "Txt Test.$type.local" SRV
Run >SYS:txt-query-host.log $c/dnssd -q arosbox.local A
Wait 14
Break \`Status COMMAND=$c/dnssd\` C
Wait 1
Break \`Status COMMAND=$c/dnssd\` C
Wait 1
Break \`Status COMMAND=$c/dnssd\` C
Wait 1
Break \`Status COMMAND=$c/dnssd\` C
Wait 3
Shutdown
RUNEOF
printf 'Run >NIL: Execute S:txt-run\n' > "$sys/S/User-Startup"

(timeout 60 avahi-publish -s "Host Txt" "$type" 6000 "name=Host With Spaces" "k=v" \
    > "$work/publish.log" 2>&1 || true) &
(sleep 22; timeout 12 avahi-browse -rpt "$type" > "$work/browse.log" 2>&1 || true) &
(cd "$sys" && timeout 80 xvfb-run -a boot/linux/AROSBootstrap \
    > "$work/serial.log" 2>&1) || true
wait || true
cp "$sys/txt-register.log" "$sys/txt-browse.log" "$sys/txt-query.log" \
    "$sys/txt-query-host.log" "$work/" 2> /dev/null || true

status=0
# avahi-browse -p ends a resolved line with the record's strings, each in
# quotes, in the order of the record.
line=$(grep '^=;aroshost;IPv4;Txt\\032Test;' "$work/browse.log" 2> /dev/null | head -1)
if echo "$line" | grep -q '"UMPEndpointName=AROS MIDI Hub"' &&
   echo "$line" | grep -q '"ProductInstanceId=abc-1"' &&
   echo "$line" | grep -q '"note=two  spaces"' &&
   echo "$line" | grep -qF '"path=C:\Dir"'; then
    echo "ok: Avahi read the four items AROS registered, spaces and backslash kept"
else
    echo "FAIL: what AROS registered did not arrive as written: $line"
    status=1
fi
# On AROS an item's own spaces come with a backslash before them.
if grep -qF 'Host\032Txt' "$work/txt-browse.log" 2> /dev/null ||
   grep -q 'Host Txt' "$work/txt-browse.log" 2> /dev/null; then
    if grep 'Host Txt' "$work/txt-browse.log" | grep -qF 'name=Host\ With\ Spaces k=v'; then
        echo "ok: AROS was given the host's two items, the spaces of the first marked as its own"
    else
        echo "FAIL: the host's text reached AROS otherwise: $(grep 'Host Txt' "$work/txt-browse.log" | grep TXT | head -1)"
        status=1
    fi
else
    echo "FAIL: AROS did not see the host's service"
    status=1
fi
# A question about what this host itself registered, asked on this host:
# the daemon does not hear itself, and has to answer from what it holds.
if grep -q 'type 33 .*5000' "$work/txt-query.log" 2> /dev/null &&
   grep -q 'type 1 .*192\.168\.166\.2' "$work/txt-query-host.log"; then
    echo "ok: a client on AROS that asks for a service's SRV record and the host's address, both this host's own, is given them"
else
    echo "FAIL: this host's own records, asked for on it: $(head -2 "$work/txt-query.log" 2> /dev/null)"
    status=1
fi
echo "logs: $work"
exit $status
