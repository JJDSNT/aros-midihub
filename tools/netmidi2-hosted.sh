#!/bin/sh
# Runs the UMP path with AROS at one end: MIDIHub inside Linux-hosted AROS,
# listening for Network MIDI 2.0 on a port of its own, and
# tests/interop/netmidi2_peer.c on the host as its Client, over the veth
# pair the mDNS runners use. It is tools/netmidi2-host.sh with AROS's
# stack and CAMD in the path. Both ends are our own code: this shows the
# path whole, not that another implementation agrees with it.
#
#   tools/netmidi2-hosted.sh [AROS_DIR [WORK_DIR]]
#
# eth1 is AROS's (192.168.166.2), aroshost the host's (192.168.166.1). It
# needs passwordless sudo, ethtool and xvfb-run.
#
# AROS starts "MIDIHub --midi2 5673 5004" and a CAMD program listening on
# the cluster "MIDIHub In". The host invites AROS and sends a Note On, a
# Note Off, a Control Change and a SysEx message, as UMPs: the CAMD program
# has to print the four as MIDI 1.0, and All Notes Off on the sixteen
# channels once the host has said Bye. A second CAMD program plays a
# scale and a SysEx message into "MIDIHub Out": the host has to print
# them, having received UMPs.
# Exit status: 0 when both directions passed.
set -eu

here=$(cd "$(dirname "$0")/.." && pwd)
aros=$(realpath "${1:-$here/build-aros-linux/bin/linux-x86_64/AROS}")
work=$(realpath "${2:-$(mktemp -d)}")
sys="$work/AROS"
peer="$work/netmidi2-peer"
c=SYS:Extras/aros-midihub/C

test -x "$aros/boot/linux/AROSBootstrap"
test -f "$aros/Extras/aros-midihub/C/MIDIHub"
test -f "$aros/Extras/aros-midihub/C/MIDIHubCAMDProbe"

mkdir -p "$work"
cc -I"$here/include" -O2 -Wall -Wextra -Werror -std=c99 -o "$peer" \
    "$here/tests/interop/netmidi2_peer.c" "$here/src/netmidi2.c" \
    "$here/src/ump.c"

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
cp -a --reflink=auto "$aros" "$sys"
db="$sys/System/Network/AROSTCP/db"
echo 'eth1 DEV=DEVS:networks/eth.device UNIT=1 IP=192.168.166.2 NETMASK=255.255.255.0 UP' \
    >> "$db/interfaces"
printf 'HOST 192.168.166.2 arosbox.arosnet arosbox\nDOMAIN arosnet 192.168.166.\n' \
    > "$db/netdb-myhost"
sed -i 's/OPENGUI=.*/OPENGUI=NO/' "$db/general.config"
sudo setcap cap_net_raw,cap_net_admin+ep "$sys/boot/linux/AROSBootstrap"
cat > "$sys/S/netmidi2-run" <<RUNEOF
FailAt 21
CD SYS:System/Network/AROSTCP
Path C S ADD QUIET
Run >NIL: QUIET AROSTCP
WaitForPort AROSTCP
Wait 8
Run >SYS:hub.log $c/MIDIHub --midi2 5673 5004
Wait 2
Run >SYS:camd-in.log $c/MIDIHubCAMDProbe --monitor "MIDIHub In" 40
Wait 16
$c/MIDIHubCAMDProbe >SYS:camd-out.log --send "MIDIHub Out"
Wait 14
Shutdown
RUNEOF
printf 'Run >NIL: Execute S:netmidi2-run\n' > "$sys/S/User-Startup"

(
    for attempt in $(seq 1 60); do
        ping -c 1 -W 1 192.168.166.2 > /dev/null 2>&1 && break
        sleep 1
    done
    sleep 12
    timeout 30 "$peer" 5674 192.168.166.2 5673 --send > "$work/peer.log" 2>&1
) &
(cd "$sys" && timeout 120 xvfb-run -a boot/linux/AROSBootstrap \
    > "$work/serial.log" 2>&1) || true
wait || true
cp "$sys/hub.log" "$sys/camd-in.log" "$sys/camd-out.log" "$work/" 2> /dev/null || true

status=0
if grep -q 'MIDI 2.0 session established peer="Peer on 5674"' "$work/hub.log" 2> /dev/null &&
   grep -q '^CAMDProbe: 90 3c 64$' "$work/camd-in.log" &&
   grep -q '^CAMDProbe: 80 3c 40$' "$work/camd-in.log" &&
   grep -q '^CAMDProbe: b0 07 64$' "$work/camd-in.log" &&
   grep -q '^CAMDProbe: SysEx 13 bytes: f0 7d 01 02 03 04 05 06 07 08 09 0a f7' "$work/camd-in.log" &&
   [ "$(grep -c '^CAMDProbe: b[0-9a-f] 7b 00$' "$work/camd-in.log")" = 16 ]; then
    echo "ok: UMPs from the host reached a CAMD program on AROS as a Note On, a Note Off, a Control Change and a SysEx message, and its Bye was followed by All Notes Off on every channel"
else
    echo "FAIL: from the host to CAMD"
    status=1
fi
if grep -q '^established slot=0 role=client peer="AROS MIDIHub"' "$work/peer.log" 2> /dev/null &&
   grep -q '^midi from=0 903c64$' "$work/peer.log" &&
   grep -q '^midi from=0 804800$' "$work/peer.log" &&
   [ "$(grep -c '^midi from=0 9' "$work/peer.log")" = 8 ] &&
   grep -q '^midi from=0 f07e7f0601f7$' "$work/peer.log"; then
    echo "ok: a scale and a SysEx message played by a CAMD program on AROS reached the host as UMPs, eight notes and no more"
else
    echo "FAIL: from CAMD to the host"
    status=1
fi
echo "logs: $work"
exit $status
