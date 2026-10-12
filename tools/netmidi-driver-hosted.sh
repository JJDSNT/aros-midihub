#!/bin/sh
# Checks on Linux-hosted AROS what happens between the service netmidid and
# the driver DEVS:Midi/applemidi when one of them is not what, or where,
# the other expects (AppleMIDI backlog AM26). node-rtpmidi on the host is
# the peer throughout.
#
#   tools/netmidi-driver-hosted.sh [AROS_DIR [WORK_DIR]]
#
# eth1 is AROS's (192.168.166.2), aroshost the host's (192.168.166.1). It
# needs passwordless sudo, ethtool, xvfb-run, node and npm, and the network
# once to fetch the package.
#
#   layout   The driver in DEVS:Midi is one built with another layout of
#            adapters. The service has to say so and leave it alone: a
#            session still connects, nothing reaches the port, and the
#            service is still running at the end.
#   comings  One boot, in order. A peer's four messages reach a CAMD
#            program on applemidi.in.0. With no CAMD program left, memory
#            is flushed, which unloads camd.library and the driver under
#            the running service if nothing else holds the library (the
#            result line says which happened); a second peer's messages
#            have to reach a new CAMD program all the same. The service is then
#            stopped, and a CAMD program that plays into applemidi.out.0
#            has to finish, not wait for a service that is gone. The
#            service is started again by hand and a third peer's messages
#            have to arrive.
# Exit status: 0 when both passed.
set -eu

here=$(cd "$(dirname "$0")/.." && pwd)
aros=$(realpath "${1:-$here/build-aros-linux/bin/linux-x86_64/AROS}")
work=$(realpath "${2:-$(mktemp -d)}")
sys="$work/AROS"
peer="$here/tests/interop/node_rtpmidi_peer.js"
c=SYS:Extras/aros-midihub/C

test -x "$aros/boot/linux/AROSBootstrap"
test -f "$aros/System/Network/Services/netmidid"
test -f "$aros/Devs/Midi/applemidi"
test -f "$aros/Extras/aros-midihub/C/applemidi-layout2"
test -f "$aros/System/Network/AROSTCP/db/services.d/netmidi"
test -f "$aros/Extras/aros-midihub/C/MIDIHubCAMDProbe"

mkdir -p "$work/node"
if [ ! -d "$work/node/node_modules/rtpmidi" ]; then
    (cd "$work/node" && npm init -y > /dev/null 2>&1 &&
     npm install --omit=optional --ignore-scripts --no-audit --no-fund \
         rtpmidi@1.0.0 > "$work/npm.log" 2>&1) ||
        { echo "cannot install node-rtpmidi (see $work/npm.log)"; exit 2; }
fi
export NODE_PATH="$work/node/node_modules"

if ! ip link show aroshost > /dev/null 2>&1; then
    sudo ip link add eth1 type veth peer name aroshost
    sudo ip addr add 192.168.166.1/24 dev aroshost
fi
sudo ip link set eth1 up
sudo ip link set aroshost up
sudo sysctl -qw net.ipv6.conf.eth1.disable_ipv6=1
sudo ethtool -K aroshost tx off > /dev/null
sudo ethtool -K eth1 tx off > /dev/null

prepare() {
    sudo rm -rf "$sys"
    cp -a --reflink=auto "$aros" "$sys"
    db="$sys/System/Network/AROSTCP/db"
    echo 'eth1 DEV=DEVS:networks/eth.device UNIT=1 IP=192.168.166.2 NETMASK=255.255.255.0 UP' \
        >> "$db/interfaces"
    printf 'HOST 192.168.166.2 arosbox.arosnet arosbox\nDOMAIN arosnet 192.168.166.\n' \
        > "$db/netdb-myhost"
    sed -i 's/OPENGUI=.*/OPENGUI=NO/' "$db/general.config"
    # have the service write what it would print to a file
    printf 'SYS:netmidid.log' > "$sys/Prefs/Env-Archive/netmidid.log"
    sudo setcap cap_net_raw,cap_net_admin+ep "$sys/boot/linux/AROSBootstrap"
    cat > "$sys/S/applemidi-run" <<RUNEOF
FailAt 21
CD SYS:System/Network/AROSTCP
Path C S ADD QUIET
Run >NIL: QUIET AROSTCP
WaitForPort AROSTCP
Wait 8
$1
Shutdown
RUNEOF
    printf 'Run >NIL: Execute S:applemidi-run\n' > "$sys/S/User-Startup"
}

boot() {
    (cd "$sys" && timeout "$1" xvfb-run -a boot/linux/AROSBootstrap \
        > "$work/serial-$2.log" 2>&1) || true
}

svc=SYS:System/Network/Services/netmidid
status=0

four() {
    [ "$(grep -c '^CAMDProbe: 90 45 50$' "$1" 2> /dev/null)" = 1 ] &&
    [ "$(grep -c '^CAMDProbe: 80 45 40$' "$1")" = 1 ] &&
    [ "$(grep -c '^CAMDProbe: b0 07 64$' "$1")" = 1 ] &&
    [ "$(grep -c '^CAMDProbe: SysEx 6 bytes: f0 7d 01 02 03 f7' "$1")" = 1 ]
}
# a peer that invites AROS, $1 seconds after it answers on the link
peer_after() {
    for attempt in $(seq 1 60); do
        ping -c 1 -W 1 192.168.166.2 > /dev/null 2>&1 && break
        sleep 1
    done
    sleep "$1"
    timeout 12 node "$peer" "$2" 192.168.166.2 5004 > "$work/$3" 2>&1 || true
}

# layout
prepare "Run >SYS:camd-layout.log $c/MIDIHubCAMDProbe --monitor applemidi.in.0 26
Wait 30
Status >SYS:status-layout.log"
cp "$sys/Extras/aros-midihub/C/applemidi-layout2" "$sys/Devs/Midi/applemidi"
peer_after 14 5114 layout-peer.log &
boot 90 layout
wait || true
cp "$sys/netmidid.log" "$work/netmidid-layout.log" 2> /dev/null || true
cp "$sys/camd-layout.log" "$sys/status-layout.log" "$work/" 2> /dev/null || true
if grep -q 'is of another layout than this service' "$work/netmidid-layout.log" 2> /dev/null &&
   grep -q 'session connected' "$work/netmidid-layout.log" &&
   ! grep -q '^CAMDProbe: 90 ' "$work/camd-layout.log" &&
   grep -aq 'command: netmidi$' "$work/status-layout.log"; then
    echo "ok: a driver of another layout is left alone: the service says so, serves no port of it, and goes on"
else
    echo "FAIL: a driver of another layout"
    status=1
fi

# comings
prepare "Run >SYS:camd-1.log $c/MIDIHubCAMDProbe --monitor applemidi.in.0 20
Wait 24
Avail >SYS:flush.log FLUSH
Wait 2
Run >SYS:camd-2.log $c/MIDIHubCAMDProbe --monitor applemidi.in.0 20
Wait 24
Break \`Status COMMAND=netmidi\` C
Wait 3
Copy SYS:netmidid.log SYS:netmidid-first.log
$c/MIDIHubCAMDProbe >SYS:camd-send.log --send applemidi.out.0
Echo >SYS:send-done.log done
Run >NIL: $svc
Wait 4
Run >SYS:camd-3.log $c/MIDIHubCAMDProbe --monitor applemidi.in.0 20
Wait 24"
(
    peer_after 12 5114 peer-1.log
    sleep 12
    timeout 12 node "$peer" 5124 192.168.166.2 5004 > "$work/peer-2.log" 2>&1 || true
    sleep 26
    timeout 12 node "$peer" 5134 192.168.166.2 5004 > "$work/peer-3.log" 2>&1 || true
) &
boot 170 comings
wait || true
for f in netmidid.log netmidid-first.log camd-1.log camd-2.log camd-3.log \
         camd-send.log send-done.log flush.log; do
    cp "$sys/$f" "$work/" 2> /dev/null || true
done
if four "$work/camd-1.log"; then
    echo "ok: a peer's messages reached a CAMD program on the driver's port"
else
    echo "FAIL: the first peer"
    status=1
fi
if four "$work/camd-2.log"; then
    if grep -aq 'was unloaded' "$work/netmidid-first.log" 2> /dev/null; then
        echo "ok: memory was flushed with no CAMD program open and the driver was unloaded under the service, which followed the new one: a second peer's messages arrived"
    else
        echo "ok: after memory was flushed with no CAMD program open, a second peer's messages arrived (camd.library stayed in memory: the driver was not unloaded, and that case was not run)"
    fi
else
    echo "FAIL: after the flush"
    status=1
fi
if grep -q '^done$' "$work/send-done.log" 2> /dev/null &&
   grep -q 'playing a C major scale' "$work/camd-send.log"; then
    echo "ok: with the service stopped, a CAMD program that plays into the port finishes"
else
    echo "FAIL: playing into a port nobody serves"
    status=1
fi
if four "$work/camd-3.log"; then
    echo "ok: the service started again took the ports up, and a third peer's messages arrived"
else
    echo "FAIL: after the service was started again"
    status=1
fi
echo "logs: $work"
exit $status
