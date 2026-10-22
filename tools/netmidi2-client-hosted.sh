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
# reach the Host. AROS's own Host has to be advertised as _midi2._udp with
# its endpoint's name and product id. That is the case "messages"; the case
# "again" is this:
#
#   again   The Host is advertised with nothing behind it until AROS has
#           invited it four times, so that its wait between invitations has
#           grown; then the Host answers, and 8 s into the session it ends
#           it with Bye. AROS has to invite again from the first wait, not
#           after the longest it had reached (#1796): a second Host, there
#           for the 22 s after the first one left, has to be invited.
#
# NETMIDI2_CASES, a list of the cases' names, runs only those.
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

cases=${NETMIDI2_CASES:-messages again}
want() {
    case " $cases " in *" $1 "*) return 0 ;; esac
    return 1
}
# a cut of the service that looks for nobody by name (N7, N8) is told the
# Host's address
discovery=1
grep -aq 'MIDI 2.0 host "%s"' "$aros/System/Network/Services/netmidid" || discovery=0

prepare() {
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
}

status=0
if want messages; then
prepare
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
    # what AROS advertises of its own Host, seen by the host's Avahi
    [ $discovery = 0 ] ||
    timeout 40 avahi-browse -rp _midi2._udp > "$work/browse.log" 2>&1 &
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
if [ $discovery = 1 ]; then
    if grep -q '^=;aroshost;IPv4;AROS\\032Net\\032MIDI;_midi2._udp;local;[^;]*;192\.168\.166\.2;5673;.*"UMPEndpointName=AROS Net MIDI"' "$work/browse.log" 2> /dev/null &&
       grep '^=;aroshost;IPv4;AROS\\032Net\\032MIDI;_midi2._udp' "$work/browse.log" | grep -q '"ProductInstanceId=AROS-kept000000000001"'; then
        echo "ok: Avahi resolved AROS's own Host, _midi2._udp at 192.168.166.2 port 5673, with its endpoint's name and product id in the text record"
    else
        echo "FAIL: AROS's Host was not advertised as _midi2._udp (see $work/browse.log)"
        status=1
    fi
fi
fi

# again: the session ends with Bye, the Host is still advertised. The host
# reads the service's log as it is written to know when.
if want again; then
prepare
cat > "$sys/S/netmidi2-run" <<RUNEOF
FailAt 21
CD SYS:System/Network/AROSTCP
Path C S ADD QUIET
Run >NIL: QUIET AROSTCP
WaitForPort AROSTCP
Wait 8
Wait 150
Shutdown
RUNEOF
printf 'Run >NIL: Execute S:netmidi2-run\n' > "$sys/S/User-Startup"
log="$sys/netmidid.log"
# how many lines say it so far; 0 before the log is there
count() { n=$(grep -c "$1" "$log" 2> /dev/null) || true; echo "${n:-0}"; }
(
    [ $discovery = 0 ] ||
    timeout 200 avahi-publish -s "0a1b-host" _midi2._udp 5683 \
        "UMPEndpointName=Host Endpoint" "ProductInstanceId=0a1b" \
        > "$work/publish-again.log" 2>&1 &
    # nobody answers four invitations: AROS now waits a while for the next
    for attempt in $(seq 1 150); do
        [ "$(count 'inviting a MIDI 2.0 host')" -ge 4 ] && break
        sleep 1
    done
    "$peer" 5683 > "$work/again-peer-1.log" 2>&1 &
    first=$!
    for attempt in $(seq 1 70); do
        [ "$(count 'MIDI 2.0 session established')" -ge 1 ] && break
        sleep 1
    done
    sleep 8
    # sent SIGTERM, the Host ends the session with Bye
    kill "$first" 2> /dev/null || true
    wait "$first" 2> /dev/null || true
    timeout 22 "$peer" 5683 > "$work/again-peer-2.log" 2>&1 || true
    wait
) &
(cd "$sys" && timeout 220 xvfb-run -a boot/linux/AROSBootstrap \
    > "$work/serial-again.log" 2>&1) || true
wait || true
cp "$sys/netmidid.log" "$work/hub-again.log" 2> /dev/null || true
if grep -q '^established slot=' "$work/again-peer-1.log" 2> /dev/null &&
   grep -q '^established slot=' "$work/again-peer-2.log" &&
   [ "$(grep -c 'MIDI 2.0 session established' "$work/hub-again.log")" -ge 2 ]; then
    echo "ok: after the Host ended the session, the service invited it again from the first wait"
else
    echo "FAIL: the Host was not invited again in time (#1796)"
    status=1
fi
fi
echo "logs: $work"
exit $status
