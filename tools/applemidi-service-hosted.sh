#!/bin/sh
# Checks on Linux-hosted AROS that the AppleMIDI runtime runs as a service
# of the AROSTCP service manager: nobody starts it, the stack does, from
# db/services.d/netmidi, with no console and no arguments. node-rtpmidi
# on the host is its peer.
#
#   tools/applemidi-service-hosted.sh [AROS_DIR [WORK_DIR]]
#
# eth1 is AROS's (192.168.166.2), aroshost the host's (192.168.166.1). It
# needs passwordless sudo, ethtool, xvfb-run, node and npm, a running
# avahi-daemon and avahi-utils, and the network once to fetch the package.
#
# Two peers on the host invite AROS, one of them under a name a port is
# kept for in the service's configuration (ENVARC:NetMIDI, "applemidi.port.3 =
# Kept peer"). Each sends a Note On, a Note Off, a Control Change and a
# SysEx message; a CAMD program listening on applemidi.in.3 has to print
# the four of the named peer, and one on applemidi.in.0 those of the
# other, which took the first free port. A third plays a scale and a
# SysEx message into applemidi.out.3: only the named peer may show them.
# The service is no client of camd.library: the adapters of the driver
# DEVS:Midi/applemidi are its only way to CAMD. The host's Avahi has to
# see the session advertised. The service is then told to stop, and has
# to be gone. In a second boot the configuration names the host as a peer
# to invite: the service has to invite it, and a scale sent to
# applemidi.out.0 has to arrive there.
# Exit status: 0 when all of it passed.
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

status=0
prepare "Run >SYS:camd-0.log $c/MIDIHubCAMDProbe --monitor applemidi.in.0 40
Run >SYS:camd-3.log $c/MIDIHubCAMDProbe --monitor applemidi.in.3 40
Wait 20
$c/MIDIHubCAMDProbe >SYS:camd-send.log --send applemidi.out.3
Wait 4
NetMIDIStatus >SYS:counts.log
Wait 18
Status >SYS:status-before.log
Break \`Status COMMAND=netmidi\` C
Wait 3
Status >SYS:status-after.log"
# port 3 is kept for the peer of that name; the other takes the first free
printf 'applemidi.port.3 = Kept peer\n' > "$sys/Prefs/Env-Archive/NetMIDI"
(
    for attempt in $(seq 1 60); do
        ping -c 1 -W 1 192.168.166.2 > /dev/null 2>&1 && break
        sleep 1
    done
    sleep 14
    timeout 6 avahi-browse -rpt _apple-midi._udp > "$work/browse.log" 2>&1 &
    PEER_NAME="Kept peer" timeout 30 node "$peer" 5124 192.168.166.2 5004 \
        > "$work/peer-kept.log" 2>&1 &
    timeout 30 node "$peer" 5114 192.168.166.2 5004 > "$work/peer.log" 2>&1
    wait
) &
boot 130 service
wait || true
cp "$sys/netmidid.log" "$sys/camd-0.log" "$sys/camd-3.log" "$sys/camd-send.log" "$sys/counts.log" \
    "$sys/status-before.log" "$sys/status-after.log" "$work/" 2> /dev/null || true

# the four messages, once: each port has one peer's and not the other's
four() {
    [ "$(grep -c '^CAMDProbe: 90 45 50$' "$1" 2> /dev/null)" = 1 ] &&
    [ "$(grep -c '^CAMDProbe: 80 45 40$' "$1")" = 1 ] &&
    [ "$(grep -c '^CAMDProbe: b0 07 64$' "$1")" = 1 ] &&
    [ "$(grep -c '^CAMDProbe: SysEx 6 bytes: f0 7d 01 02 03 f7' "$1")" = 1 ]
}
if grep -q 'session connected port=0' "$work/netmidid.log" 2> /dev/null &&
   grep -q 'session connected port=3' "$work/netmidid.log" &&
   four "$work/camd-0.log" && four "$work/camd-3.log" &&
   grep -q '^control receiver_feedback$' "$work/peer.log"; then
    echo "ok: the stack started the service; of two node-rtpmidi peers the one a port is kept for got port 3 and the other port 0, and a CAMD program on each port received its peer's four messages and not the other's"
else
    echo "FAIL: the service started by the stack, towards CAMD"
    status=1
fi
# the scale's first and last notes, and the SysEx: to the peer of port 3 only
if grep -q '^message 903c64$' "$work/peer-kept.log" 2> /dev/null &&
   grep -q '^message 804800$' "$work/peer-kept.log" &&
   grep -q '^message f07e7f0601f7$' "$work/peer-kept.log" &&
   ! grep -q '^message ' "$work/peer.log"; then
    echo "ok: a scale and a SysEx a CAMD program sent to applemidi.out.3 reached that port's peer and not the other"
else
    echo "FAIL: from the driver's port to the network"
    status=1
fi
# What the service counted, asked of it by a command: the four messages
# of each peer in one packet, and to the peer of port 3 the scale of
# sixteen notes and the SysEx message that were played to it.
if grep -q '^applemidi\.0 "Node peer": in 4 messages in 1 packets, 0 lost; out 0 messages' "$work/counts.log" 2> /dev/null &&
   grep -q '^applemidi\.3 "Kept peer": in 4 messages in 1 packets, 0 lost; out 17 messages' "$work/counts.log"; then
    echo "ok: NetMIDIStatus prints, for each port, its peer's name and what was counted: four messages in, and seventeen out on the port played to"
else
    echo "FAIL: the service's counts: $(cat "$work/counts.log" 2> /dev/null | head -3)"
    status=1
fi
if grep -q '^=;aroshost;IPv4;.*;_apple-midi._udp;.*;192.168.166.2;5004;' "$work/browse.log" 2> /dev/null; then
    echo "ok: Avahi saw the session it advertises"
else
    echo "FAIL: the service's advertisement"
    status=1
fi
if grep -aq 'command: netmidi$' "$work/status-before.log" 2> /dev/null &&
   ! grep -aq 'command: netmidi$' "$work/status-after.log" 2> /dev/null; then
    echo "ok: told to stop, it stopped"
else
    echo "FAIL: stopping the service"
    status=1
fi

# The other way round: the configuration names a peer to invite, by
# address. The host listens before AROS is up.
prepare "Wait 14
$c/MIDIHubCAMDProbe >SYS:camd-send-invite.log --send applemidi.out.0
Wait 10"
printf 'peer_ip = 192.168.166.1\npeer_port = 5104\n' > "$sys/Prefs/Env-Archive/NetMIDI"
timeout 80 node "$peer" 5104 > "$work/invite-peer.log" 2>&1 &
boot 85 invite
kill %1 2> /dev/null || true
wait || true
cp "$sys/netmidid.log" "$work/netmidid-invite.log" 2> /dev/null || true
if grep -q 'session connected port=0' "$work/netmidid-invite.log" 2> /dev/null &&
   grep -q 'clock exchange completed' "$work/netmidid-invite.log" &&
   grep -q '^message 903c64$' "$work/invite-peer.log" &&
   grep -q '^message 804800$' "$work/invite-peer.log"; then
    echo "ok: the service invited the peer its configuration names, exchanged clocks, and a scale sent to applemidi.out.0 arrived there"
else
    echo "FAIL: the service inviting a configured peer"
    status=1
fi
echo "logs: $work"
exit $status
