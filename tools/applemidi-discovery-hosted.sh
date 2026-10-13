#!/bin/sh
# Checks on Linux-hosted AROS that the AppleMIDI service's session is
# advertised by the system's mDNS service, mdnsd through bonami.library,
# and by nothing of its own: it holds no socket on port 5353. The service
# is netmidid, started by the stack from db/services.d/netmidi.
#
#   tools/applemidi-discovery-hosted.sh [AROS_DIR [WORK_DIR]]
#
# eth1 is AROS's (192.168.166.2), aroshost the host's (192.168.166.1), with
# the host's Avahi browsing on it. It needs passwordless sudo, a running
# avahi-daemon, avahi-utils, ethtool and xvfb-run.
#
#   advertised  The service starts with the stack. Its configuration is
#               changed under it, and it is then sent Ctrl-C. Avahi has to
#               resolve the session, named after the machine, of type
#               _apple-midi._udp to AROS's address and port 5004, then
#               see it replaced by the one the new configuration names,
#               and be told that that one is gone.
#   found       The host advertises a session "Host Session" through Avahi
#               and runs node-rtpmidi behind it. The service's
#               configuration says "peer_name = Host Session": it has to find
#               the session by its name and invite it. The host then takes
#               the session and its advertisement away and brings both
#               back: AROS has to see it go, see it return, and invite
#               again.
#
# The second case needs node and npm, and the network once, to fetch
# node-rtpmidi into WORK_DIR.
# Exit status: 0 when all of it was seen.
set -eu

here=$(cd "$(dirname "$0")/.." && pwd)
aros=$(realpath "${1:-$here/build-aros-linux/bin/linux-x86_64/AROS}")
work=$(realpath "${2:-$(mktemp -d)}")
sys="$work/AROS"
c=SYS:Extras/aros-midihub/C

test -x "$aros/boot/linux/AROSBootstrap"
test -f "$aros/System/Network/Services/netmidid"
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

mkdir -p "$work"
prepare() {
    sudo rm -rf "$sys"
    cp -a --reflink=auto "$aros" "$sys"
    db="$sys/System/Network/AROSTCP/db"
    echo 'eth1 DEV=DEVS:networks/eth.device UNIT=1 IP=192.168.166.2 NETMASK=255.255.255.0 UP' \
        >> "$db/interfaces"
    printf 'HOST 192.168.166.2 arosbox.arosnet arosbox\nDOMAIN arosnet 192.168.166.\n' \
        > "$db/netdb-myhost"
    sed -i 's/OPENGUI=.*/OPENGUI=NO/' "$db/general.config"
    # what the service would print, to a file
    # upstream's cut installs the service disabled, as it does sshd
    sed -i 's/^Enabled=.*/Enabled=yes/' "$sys/System/Network/AROSTCP/db/services.d/netmidi"
    printf 'SYS:netmidid.log' > "$sys/Prefs/Env-Archive/netmidid.log"
    sudo setcap cap_net_raw,cap_net_admin+ep "$sys/boot/linux/AROSBootstrap"
}

prepare
cat > "$sys/S/discovery-run" <<RUNEOF
FailAt 21
CD SYS:System/Network/AROSTCP
Path C S ADD QUIET
Run >NIL: QUIET AROSTCP
WaitForPort AROSTCP
Wait 8
Wait 14
Echo >ENV:NetMIDI "session_name = Renamed Session"
Wait 14
Break \`Status COMMAND=netmidi\` C
Wait 6
Shutdown
RUNEOF
printf 'Run >NIL: Execute S:discovery-run\n' > "$sys/S/User-Startup"

# Every mDNS packet on the link is kept: this case has failed once in
# twenty-five runs, Avahi seeing the service and not resolving it (mDNS backlog,
# MD20), and the capture of the run that fails is what will say why.
(sudo timeout 80 tcpdump -i aroshost -n -l -v udp port 5353 > "$work/mdns-capture.txt" 2> /dev/null || true) &
(sleep 4; timeout 70 avahi-browse -rp _apple-midi._udp > "$work/browse.log" 2>&1 || true) &
(cd "$sys" && timeout 90 xvfb-run -a boot/linux/AROSBootstrap \
    > "$work/serial.log" 2>&1) || true
wait || true
cp "$sys/netmidid.log" "$work/hub.log" 2> /dev/null || true

status=0
if grep -q 'advertised by the system mDNS service' "$work/hub.log" 2> /dev/null &&
   grep -q '^=;aroshost;IPv4;arosbox;_apple-midi._udp;local;[^;]*;192\.168\.166\.2;5004;' "$work/browse.log"; then
    echo "ok: Avahi resolved \"arosbox\", the session named after the machine (_apple-midi._udp) to 192.168.166.2 port 5004, advertised by mdnsd"
else
    echo "FAIL: the session was not advertised (keep $work/mdns-capture.txt and $work/browse.log)"
    status=1
fi
if grep -q 'the configuration changed: beginning anew' "$work/hub.log" 2> /dev/null &&
   grep -q '^-;aroshost;IPv4;arosbox;_apple-midi._udp;local' "$work/browse.log" &&
   grep -q '^=;aroshost;IPv4;Renamed\\032Session;_apple-midi._udp;local;[^;]*;192\.168\.166\.2;5004;' "$work/browse.log"; then
    echo "ok: a name written to ENV:NetMIDI was taken without anyone restarting the service: the old session left the network and the new one came"
else
    echo "FAIL: the changed configuration was not taken"
    status=1
fi
if grep -q '^-;aroshost;IPv4;Renamed\\032Session;_apple-midi._udp;local' "$work/browse.log"; then
    echo "ok: when the service ended, Avahi was told the session is gone"
else
    echo "FAIL: no goodbye for the session"
    status=1
fi
# found: the host's session, by its name
mkdir -p "$work/node"
if [ ! -d "$work/node/node_modules/rtpmidi" ]; then
    (cd "$work/node" && npm init -y > /dev/null 2>&1 &&
     npm install --omit=optional --ignore-scripts --no-audit --no-fund \
         rtpmidi@1.0.0 > "$work/npm.log" 2>&1) ||
        { echo "cannot install node-rtpmidi (see $work/npm.log)"; exit 2; }
fi
export NODE_PATH="$work/node/node_modules"
peer="$here/tests/interop/node_rtpmidi_peer.js"

prepare
cat > "$sys/S/discovery-run" <<RUNEOF
FailAt 21
CD SYS:System/Network/AROSTCP
Path C S ADD QUIET
Run >NIL: QUIET AROSTCP
WaitForPort AROSTCP
Wait 8
Wait 50
Break \`Status COMMAND=netmidi\` C
Wait 4
Shutdown
RUNEOF
printf 'Run >NIL: Execute S:discovery-run\n' > "$sys/S/User-Startup"
printf 'peer_name = Host Session\n' > "$sys/Prefs/Env-Archive/NetMIDI"

(
    # there from the start: the session and what advertises it
    timeout 26 avahi-publish -s "Host Session" _apple-midi._udp 5104 \
        > "$work/publish-1.log" 2>&1 &
    timeout 26 node "$peer" 5104 > "$work/found-peer-1.log" 2>&1 &
    for attempt in $(seq 1 60); do
        ping -c 1 -W 1 192.168.166.2 > /dev/null 2>&1 && break
        sleep 1
    done
    wait
    # both gone for a while, then back
    sleep 8
    timeout 30 avahi-publish -s "Host Session" _apple-midi._udp 5104 \
        > "$work/publish-2.log" 2>&1 &
    timeout 30 node "$peer" 5104 > "$work/found-peer-2.log" 2>&1
    wait
) &
(cd "$sys" && timeout 120 xvfb-run -a boot/linux/AROSBootstrap \
    > "$work/serial-found.log" 2>&1) || true
wait || true
cp "$sys/netmidid.log" "$work/hub-found.log" 2> /dev/null || true

if grep -q 'session "Host Session" found at 192\.168\.166\.1 port 5104' "$work/hub-found.log" 2> /dev/null &&
   grep -q 'inviting "Host Session"' "$work/hub-found.log" &&
   grep -q 'session connected' "$work/hub-found.log" &&
   grep -q '^control invitation$' "$work/found-peer-1.log"; then
    echo "ok: AROS found the host's session by its name and invited it"
else
    echo "FAIL: the host's session was not found and invited"
    status=1
fi
if grep -q 'session "Host Session" is gone' "$work/hub-found.log" 2> /dev/null &&
   [ "$(grep -c 'session connected' "$work/hub-found.log")" -ge 2 ] &&
   grep -q '^control invitation$' "$work/found-peer-2.log"; then
    echo "ok: when the session left and came back, AROS saw both and invited it again"
else
    echo "FAIL: the session's return was not followed"
    status=1
fi
echo "logs: $work"
exit $status
