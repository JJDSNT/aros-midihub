#!/bin/sh
# Checks on Linux-hosted AROS that MIDIHub's AppleMIDI session is advertised
# by the system's mDNS service, mdnsd through bonami.library, and by nothing
# of MIDIHub's own: the program holds no socket on port 5353.
#
#   tools/applemidi-discovery-hosted.sh [AROS_DIR [WORK_DIR]]
#
# eth1 is AROS's (192.168.166.2), aroshost the host's (192.168.166.1), with
# the host's Avahi browsing on it. It needs passwordless sudo, a running
# avahi-daemon, avahi-utils, ethtool and xvfb-run.
#
# AROS starts "MIDIHub 5004" and, twenty seconds later, sends it Ctrl-C.
# Avahi has to resolve "AROS MIDIHub" of type _apple-midi._udp to AROS's
# address and port 5004, and then be told that it is gone.
# Exit status: 0 when both were seen.
set -eu

here=$(cd "$(dirname "$0")/.." && pwd)
aros=$(realpath "${1:-$here/build-aros-linux/bin/linux-x86_64/AROS}")
work=$(realpath "${2:-$(mktemp -d)}")
sys="$work/AROS"
c=SYS:Extras/aros-midihub/C

test -x "$aros/boot/linux/AROSBootstrap"
test -f "$aros/Extras/aros-midihub/C/MIDIHub"
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
cat > "$sys/S/discovery-run" <<RUNEOF
FailAt 21
CD SYS:System/Network/AROSTCP
Path C S ADD QUIET
Run >NIL: QUIET AROSTCP
WaitForPort AROSTCP
Wait 8
Run >SYS:hub.log $c/MIDIHub 5004
Wait 20
Break \`Status COMMAND=$c/MIDIHub\` C
Wait 6
Shutdown
RUNEOF
printf 'Run >NIL: Execute S:discovery-run\n' > "$sys/S/User-Startup"

(sleep 4; timeout 70 avahi-browse -rp _apple-midi._udp > "$work/browse.log" 2>&1 || true) &
(cd "$sys" && timeout 90 xvfb-run -a boot/linux/AROSBootstrap \
    > "$work/serial.log" 2>&1) || true
wait || true
cp "$sys/hub.log" "$work/" 2> /dev/null || true

status=0
# avahi-browse -p writes the space of "AROS MIDIHub" as \032.
if grep -q 'advertised by the system mDNS service' "$work/hub.log" 2> /dev/null &&
   grep -q '^=;aroshost;IPv4;AROS\\032MIDIHub;_apple-midi._udp;local;[^;]*;192\.168\.166\.2;5004;' "$work/browse.log"; then
    echo "ok: Avahi resolved \"AROS MIDIHub\" (_apple-midi._udp) to 192.168.166.2 port 5004, advertised by mdnsd"
else
    echo "FAIL: the session was not advertised"
    status=1
fi
if grep -q '^-;aroshost;IPv4;AROS\\032MIDIHub;_apple-midi._udp;local' "$work/browse.log"; then
    echo "ok: when MIDIHub ended, Avahi was told the session is gone"
else
    echo "FAIL: no goodbye for the session"
    status=1
fi
echo "logs: $work"
exit $status
