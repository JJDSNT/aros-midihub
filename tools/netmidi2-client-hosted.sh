#!/bin/sh
# Checks on Linux-hosted AROS that the service netmidid is a Network MIDI
# 2.0 Client of a Host it finds by name: the Host is advertised through the
# host machine's Avahi as _midi2._udp, and the service's configuration
# says "midi2_peer_name = Host Endpoint", the UMPEndpointName of the Host's
# text record, not the name of its service instance.
#
#   tools/netmidi2-client-hosted.sh [AROS_DIR [WORK_DIR]]
#
# eth1 is AROS's (192.168.166.2), aroshost the host's (192.168.166.1). It
# needs passwordless sudo, ethtool, xvfb-run, a running avahi-daemon and
# avahi-utils.
#
# The Host is tests/interop/netmidi2_peer.c on port 5683. AROS has to find
# it, invite it, and hand its Note On, Note Off, Control Change and SysEx
# message to a CAMD program on networkmidi2.in.0; a scale and a SysEx
# message a second CAMD program plays into networkmidi2.out.0 have to
# reach the Host.
# Exit status: 0 when all of it passed.
set -eu

here=$(cd "$(dirname "$0")/.." && pwd)
aros=$(realpath "${1:-$here/build-aros-linux/bin/linux-x86_64/AROS}")
work=$(realpath "${2:-$(mktemp -d)}")
sys="$work/AROS"
peer="$work/netmidi2-peer"
c=SYS:Extras/aros-midihub/C

test -x "$aros/boot/linux/AROSBootstrap"
test -f "$aros/System/Network/Services/netmidid"
test -f "$aros/Devs/Midi/networkmidi2"
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
# upstream's cut installs the service disabled, as it does sshd
sed -i 's/^Enabled=.*/Enabled=yes/' "$db/services.d/netmidi"
# the cut of the service without discovery (N7) is told the Host's address
discovery=1
grep -aq '_midi2._udp' "$aros/System/Network/Services/netmidid" || discovery=0
# the service's configuration, and what it would print to a file
if [ $discovery = 1 ]; then
    printf 'midi2_port = 5673\nsession_name = AROS Net MIDI\nmidi2_peer_name = Host Endpoint\n' > "$sys/Prefs/Env-Archive/NetMIDI"
else
    printf 'midi2_port = 5673\nsession_name = AROS Net MIDI\nmidi2_peer_ip = 192.168.166.1\nmidi2_peer_port = 5683\n' > "$sys/Prefs/Env-Archive/NetMIDI"
fi
printf 'SYS:netmidid.log' > "$sys/Prefs/Env-Archive/netmidid.log"
# a product id made before: it has to be the one used
printf 'AROS-kept000000000001' > "$sys/Prefs/Env-Archive/NetMIDI.id"
sudo setcap cap_net_raw,cap_net_admin+ep "$sys/boot/linux/AROSBootstrap"
cat > "$sys/S/netmidi2-run" <<RUNEOF
FailAt 21
CD SYS:System/Network/AROSTCP
Path C S ADD QUIET
Run >NIL: QUIET AROSTCP
WaitForPort AROSTCP
Wait 8
Run >SYS:camd-in.log $c/MIDIHubCAMDProbe --monitor networkmidi2.in.0 40
Wait 24
$c/MIDIHubCAMDProbe >SYS:camd-out.log --send networkmidi2.out.0
Wait 14
Shutdown
RUNEOF
printf 'Run >NIL: Execute S:netmidi2-run\n' > "$sys/S/User-Startup"

# The Host and what advertises it appear once a CAMD program on AROS is
# listening: an instance whose name is not the endpoint's, as the
# specification would have it.
(
    for attempt in $(seq 1 60); do
        ping -c 1 -W 1 192.168.166.2 > /dev/null 2>&1 && break
        sleep 1
    done
    sleep 12
    [ $discovery = 0 ] ||
    timeout 34 avahi-publish -s "0a1b-host" _midi2._udp 5683 \
        "UMPEndpointName=Host Endpoint" "ProductInstanceId=0a1b" \
        > "$work/publish.log" 2>&1 &
    timeout 34 "$peer" 5683 --send > "$work/peer.log" 2>&1
    wait
) &
(cd "$sys" && timeout 120 xvfb-run -a boot/linux/AROSBootstrap \
    > "$work/serial.log" 2>&1) || true
wait || true
cp "$sys/netmidid.log" "$work/hub.log" 2> /dev/null || true
cp "$sys/camd-in.log" "$sys/camd-out.log" "$work/" 2> /dev/null || true

status=0
if { [ $discovery = 0 ] ||
     grep -q 'MIDI 2.0 host "Host Endpoint" ("0a1b-host") found at 192.168.166.1 port 5683' "$work/hub.log" 2> /dev/null; } &&
   grep -q 'inviting a MIDI 2.0 host' "$work/hub.log" &&
   grep -q 'MIDI 2.0 session established peer="Peer on 5683" port=0' "$work/hub.log" &&
   grep -q '^established slot=0 role=host peer="AROS Net MIDI" id="AROS-kept000000000001"' "$work/peer.log"; then
    if [ $discovery = 1 ]; then
        echo "ok: the service found the Host by its endpoint's name and became its Client, under the product id it had kept"
    else
        echo "ok: the service invited the Host its configuration names by address, once it was there, and became its Client, under the product id it had kept"
    fi
else
    echo "FAIL: finding and inviting the Host"
    status=1
fi
if grep -q '^CAMDProbe: 90 3c 64$' "$work/camd-in.log" 2> /dev/null &&
   grep -q '^CAMDProbe: 80 3c 40$' "$work/camd-in.log" &&
   grep -q '^CAMDProbe: b0 07 64$' "$work/camd-in.log" &&
   grep -q '^CAMDProbe: SysEx 13 bytes: f0 7d 01 02 03 04 05 06 07 08 09 0a f7' "$work/camd-in.log"; then
    echo "ok: the Host's four messages reached a CAMD program on networkmidi2.in.0"
else
    echo "FAIL: from the Host to CAMD"
    status=1
fi
if grep -q '^midi from=0 903c64$' "$work/peer.log" 2> /dev/null &&
   grep -q '^midi from=0 804800$' "$work/peer.log" &&
   grep -q '^midi from=0 f07e7f0601f7$' "$work/peer.log"; then
    echo "ok: a scale and a SysEx message played into networkmidi2.out.0 reached the Host"
else
    echo "FAIL: from CAMD to the Host"
    status=1
fi
echo "logs: $work"
exit $status
