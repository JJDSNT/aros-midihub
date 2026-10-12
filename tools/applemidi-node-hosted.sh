#!/bin/sh
# Runs MIDIHub inside Linux-hosted AROS against node-rtpmidi on the host,
# over the veth pair the mDNS runners use. It is tools/applemidi-node-host.sh
# with AROS's stack, clock and CAMD in the path.
#
#   tools/applemidi-node-hosted.sh [AROS_DIR [WORK_DIR]]
#
# eth1 is AROS's (192.168.166.2), aroshost the host's (192.168.166.1). It
# needs passwordless sudo, ethtool, xvfb-run, node and npm, and the network
# once to fetch the package into WORK_DIR.
#
#   invite   AROS starts "MIDIHub 5004 192.168.166.1 5104 --probe-note": it
#            invites the host and sends a Note On and a Note Off, which the
#            host has to show.
#   invited  AROS starts "MIDIHub 5004" and a CAMD program that listens on
#            the cluster "MIDIHub In". The host invites AROS and sends a
#            Note On, a Note Off, a Control Change and a SysEx message. The
#            CAMD program has to print all four: they crossed the network,
#            the session, and CAMD.
#
# Each case is one boot. Exit status: 0 when both passed.
set -eu

here=$(cd "$(dirname "$0")/.." && pwd)
aros=$(realpath "${1:-$here/build-aros-linux/bin/linux-x86_64/AROS}")
work=$(realpath "${2:-$(mktemp -d)}")
sys="$work/AROS"
peer="$here/tests/interop/node_rtpmidi_peer.js"
c=SYS:Extras/aros-midihub/C

test -x "$aros/boot/linux/AROSBootstrap"
test -f "$aros/Extras/aros-midihub/C/MIDIHub"
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

# Whether the sequence number the other side says we acknowledged is the
# one of the packet it sent.
acknowledged() {
    sent=$(sed -n 's/.*RTP-MIDI seq=\([0-9]*\) .*/\1/p' "$work/hub-invited.log" 2> /dev/null | tail -1)
    test -n "$sent" && grep -q "is at $sent\\." "$work/invited-peer.log"
}

status=0

# invite: the host listens before AROS is up
prepare "Run >SYS:hub-invite.log $c/MIDIHub 5004 192.168.166.1 5104 --probe-note
Wait 14"
timeout 70 node "$peer" 5104 > "$work/invite-peer.log" 2>&1 &
boot 75 invite
kill %1 2> /dev/null || true
wait || true
cp "$sys/hub-invite.log" "$work/" 2> /dev/null || true
if grep -q 'session connected' "$work/hub-invite.log" 2> /dev/null &&
   grep -q 'clock exchange completed' "$work/hub-invite.log" &&
   grep -q '^message 903c64$' "$work/invite-peer.log" &&
   grep -q '^message 903c00$' "$work/invite-peer.log"; then
    echo "ok: AROS invited node-rtpmidi on the host, exchanged clocks, and its Note On and Note Off arrived"
else
    echo "FAIL: AROS inviting node-rtpmidi"
    status=1
fi

# invited: the host invites once AROS answers on the link
prepare "Run >SYS:hub-invited.log $c/MIDIHub 5004
Wait 2
Run >SYS:camd-invited.log $c/MIDIHubCAMDProbe --monitor \"MIDIHub In\" 30
Wait 34"
(
    for attempt in $(seq 1 60); do
        ping -c 1 -W 1 192.168.166.2 > /dev/null 2>&1 && break
        sleep 1
    done
    sleep 12
    timeout 14 node "$peer" 5114 192.168.166.2 5004 > "$work/invited-peer.log" 2>&1
) &
boot 110 invited
wait || true
cp "$sys/hub-invited.log" "$sys/camd-invited.log" "$work/" 2> /dev/null || true
if grep -q 'session connected' "$work/hub-invited.log" 2> /dev/null &&
   grep -q 'clock exchange completed' "$work/hub-invited.log" &&
   grep -q '^CAMDProbe: 90 45 50$' "$work/camd-invited.log" &&
   grep -q '^CAMDProbe: 80 45 40$' "$work/camd-invited.log" &&
   grep -q '^CAMDProbe: b0 07 64$' "$work/camd-invited.log" &&
   grep -q '^CAMDProbe: SysEx 6 bytes: f0 7d 01 02 03 f7' "$work/camd-invited.log" &&
   grep -q '^control receiver_feedback$' "$work/invited-peer.log" &&
   acknowledged; then
    echo "ok: node-rtpmidi invited AROS; a CAMD program received its Note On, Note Off, Control Change and SysEx, and AROS acknowledged"
else
    echo "FAIL: node-rtpmidi inviting AROS"
    status=1
fi
echo "logs: $work"
exit $status
