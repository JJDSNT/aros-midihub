#!/bin/sh
# Runs BonamiProbe on Linux-hosted AROS against the host's Avahi.
#
#   tools/bonami-hosted.sh [AROS_DIR [WORK_DIR]]
#
# AROS_DIR is a hosted system tree with Libs/bonami.library and
# Extras/aros-midihub/C/BonamiProbe built, by default
# build-aros-linux/bin/linux-x86_64/AROS. It is copied to WORK_DIR and not
# modified.
#
# The hosted eth.device opens a raw socket on the host interface eth<unit>.
# This script gives it one end of a veth pair: eth1 for AROS (192.168.166.2)
# and aroshost for Linux (192.168.166.1), and a second pair, eth2 and
# aroshost2 (192.168.167.x). It uses sudo to create the pairs, to
# turn off checksum offload on it (AROS would see UDP checksums the host never
# filled in) and to give the copied AROSBootstrap CAP_NET_RAW. The pair stays
# until the host restarts.
#
# mdnsd is started by the AROSTCP service manager. BonamiProbe registers
# ArosSvc, browses, and ends without withdrawing anything; the host publishes
# HostSvc and browses. The host also publishes ArosDup, a name a second
# BonamiProbe then asks for. Then the ported tools run: Share, Nearby,
# Handshake and dnssd's proxy, lookup and query modes, and the Envoy
# bridge publishes this host and inquires for an entity the host has. Needs a running avahi-daemon, avahi-utils, ethtool and
# xvfb-run.
# Exit status: 0 when each side saw the other's service with its address and
# the host saw the AROS one leave, AROS took another name for ArosDup, and
# each tool did its part.
set -eu

here=$(cd "$(dirname "$0")/.." && pwd)
aros=$(realpath "${1:-$here/build-aros-linux/bin/linux-x86_64/AROS}")
work=$(realpath "${2:-$(mktemp -d)}")
sys="$work/AROS"
type=_bonamiprobe._udp
# A name of this run only: Avahi may still hold records of an earlier one.
share=ArosShare$$
defend=ArosDefend$$
ghost=Ghost$$

test -x "$aros/boot/linux/AROSBootstrap"
test -f "$aros/Libs/bonami.library"
test -f "$aros/Extras/aros-midihub/C/BonamiProbe"
test -f "$aros/System/Network/Services/mdnsd"
test -f "$aros/System/Network/AROSTCP/db/services.d/mdns"
test -f "$aros/Devs/networks/eth.device" || test -f "$aros/Devs/Networks/eth.device"

# Two links, so that mdnsd has two interfaces: eth1/aroshost and
# eth2/aroshost2 (192.168.167.x).
link() {
    if ! ip link show "$2" > /dev/null 2>&1; then
        sudo ip link add "$1" type veth peer name "$2"
        sudo ip addr add "$3" dev "$2"
    fi
    sudo ip link set "$1" up
    sudo ip link set "$2" up
    # Linux must not answer on AROS's end of the pair with the same address.
    sudo sysctl -qw "net.ipv6.conf.$1.disable_ipv6=1"
    sudo ethtool -K "$2" tx off > /dev/null
    sudo ethtool -K "$1" tx off > /dev/null
}
link eth1 aroshost 192.168.166.1/24
link eth2 aroshost2 192.168.167.1/24

sudo rm -rf "$sys"
mkdir -p "$work"
cp -a --reflink=auto "$aros" "$sys"
db="$sys/System/Network/AROSTCP/db"
echo 'eth1 DEV=DEVS:networks/eth.device UNIT=1 IP=192.168.166.2 NETMASK=255.255.255.0 UP' \
    >> "$db/interfaces"
echo 'eth2 DEV=DEVS:networks/eth.device UNIT=2 IP=192.168.167.2 NETMASK=255.255.255.0 UP' \
    >> "$db/interfaces"
printf 'HOST 192.168.166.2 arosbox.arosnet arosbox\nDOMAIN arosnet 192.168.166.\n' \
    > "$db/netdb-myhost"
sed -i 's/OPENGUI=.*/OPENGUI=NO/' "$db/general.config"
sudo setcap cap_net_raw,cap_net_admin+ep "$sys/boot/linux/AROSBootstrap"

# Not in the boot shell: see tools/camd-compat-hosted.sh.
hostname=$(avahi-resolve -a 192.168.166.1 | cut -f2)
c=SYS:Extras/aros-midihub/C
cat > "$sys/S/bonami-run" <<EOF
FailAt 21
CD SYS:System/Network/AROSTCP
Path C S ADD QUIET
Run >NIL: QUIET AROSTCP
WaitForPort AROSTCP
Wait 3
Run >NIL: $c/BonamiProbe REGISTER ArosDup TYPE $type PORT 5005 SECONDS 12
Run >NIL: $c/BonamiProbe REGISTER $defend TYPE $type PORT 5006 SECONDS 60 UPDATE
Run >NIL: $c/BonamiProbe GHOST $ghost SECONDS 4
$c/BonamiProbe REGISTER ArosSvc TYPE $type PORT 5004 BROWSE $type SECONDS 12 LEAK >SYS:bonami-probe.log
Wait 4
Run >NIL: $c/Share $share $type 8080 "colour=amber" "size=3" QUIET
Run >NIL: $c/dnssd -P ProxySvc $type . 7000 oldbox 192.168.166.77
$c/Nearby $type TIME 5 PORTS >SYS:bonami-nearby.log
$c/Handshake HostSvc $type TIME 5 >SYS:bonami-handshake.log
$c/dnssd -G v4 $hostname >SYS:bonami-lookup.log
$c/dnssd -G v4 nobody-has-this-name >SYS:bonami-nolookup.log
$c/BonamiProbe PUBLISH ArosEntity INQUIRE Filesystem SECONDS 8 >SYS:bonami-envoy.log
ifconfig eth2 192.168.167.9 netmask 255.255.255.0
Wait 6
Run >NIL: $c/Share ${share}b $type 8081 QUIET
Run >SYS:bonami-query.log $c/dnssd -q HostSvc.$type.local SRV
Wait 4
Break \`Status COMMAND=$c/Share\` C
Wait 1
Break \`Status COMMAND=$c/Share\` C
Break \`Status COMMAND=$c/dnssd\` C
Wait 1
Break \`Status COMMAND=$c/dnssd\` C
Wait 2
Break \`Status COMMAND=SYS:System/Network/Services/mdnsd\` C
Wait 2
Shutdown
EOF
printf 'Run >NIL: Execute S:bonami-run\n' > "$sys/S/User-Startup"

(timeout 105 avahi-publish -s HostSvc "$type" 6000 k=v > "$work/publish.log" 2>&1 || true) &
(timeout 105 avahi-publish -s ArosDup "$type" 6001 > "$work/publish-dup.log" 2>&1 || true) &
(timeout 105 avahi-publish -s amigaone _envoy-nipc._tcp 376 vers=1 \
    entity=Filesystem service=Printer owner=Jaime > "$work/publish-envoy.log" 2>&1 || true) &
(sleep 6; timeout 90 avahi-browse -rp "$type" > "$work/browse.log" 2>&1 || true) &
# What Avahi holds after the service changed its text, before its name is
# contested.
(sleep 30; timeout 20 avahi-browse -rpt "$type" > "$work/browse-late.log" 2>&1 || true) &
# Half a minute in, another host claims a name mdnsd announced long before.
(sleep 38; python3 - "$defend.$type.local" 192.168.166.1 <<'PYEOF' || true
import socket, struct, sys, time
def name(n):
    return b''.join(bytes([len(l)]) + l.encode() for l in n.split('.')) + b'\0'
owner, target = name(sys.argv[1]), name('intruder.local')
rdata = struct.pack('>HHH', 0, 0, 4242) + target
packet = struct.pack('>HHHHHH', 0, 0x8400, 0, 1, 0, 0) + owner + \
    struct.pack('>HHIH', 33, 0x8001, 120, len(rdata)) + rdata
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_IF,
             socket.inet_aton(sys.argv[2]))
for _ in range(2):
    s.sendto(packet, ('224.0.0.251', 5353))
    time.sleep(1)
PYEOF
) &
(sleep 6; timeout 90 avahi-browse -rp _envoy-nipc._tcp > "$work/browse-envoy.log" 2>&1 || true) &
sleep 1
(cd "$sys" && timeout 170 xvfb-run -a boot/linux/AROSBootstrap \
    > "$work/serial.log" 2>&1) || true
wait
cp "$sys"/bonami-*.log "$work/" 2>/dev/null || true

status=0
check() {
    if grep -q "$2" "$work/$1" 2>/dev/null; then
        echo "ok: $3"
    else
        echo "FAILED: $3"
        status=1
    fi
}
check bonami-probe.log 'add "HostSvc" .* host .*\.local 192\.168\.16[67]\.1 port 6000' \
    "AROS resolved the host's service"
check browse.log "^=;aroshost;IPv4;ArosSvc;$type;local;arosbox\.local;192\.168\.166\.2;5004;" \
    "Avahi resolved the AROS service"
check browse.log "^-;aroshost;IPv4;ArosSvc;$type;local" \
    "Avahi saw the AROS service leave when its program closed the library"
# avahi-browse -p writes "ArosDup (2)" as ArosDup\032\0402\041.
check browse.log "^=;aroshost;IPv4;ArosDup.032.0402.041;$type;local;arosbox\.local;192\.168\.166\.2;5005;" \
    "AROS found the name ArosDup taken and announced ArosDup (2)"
check browse.log "^=;aroshost;IPv4;$share;$type;local;arosbox\.local;192\.168\.166\.2;8080;\"size=3\" \"colour=amber\"" \
    "Share: Avahi resolved it with its text"
check browse.log "^=;aroshost;IPv4;ProxySvc;$type;local;oldbox\.local;192\.168\.166\.77;7000;" \
    "dnssd -P: Avahi resolved a service registered for another host"
check bonami-nearby.log "HostSvc .*$hostname .*port 6000  (192\.168\.16[67]\.1)" \
    "Nearby listed the host's service"
check bonami-nearby.log "$share .*arosbox\.local .*port 8080" \
    "Nearby listed a service of this AROS"
check bonami-handshake.log "IP:    192\.168\.16[67]\.1" \
    "Handshake reached the host's service"
check bonami-lookup.log "^$hostname  192\.168\.16[67]\.1" \
    "dnssd -G found the host's address"
check bonami-nolookup.log "MDNS error -3" \
    "dnssd -G gave up on a name nobody has"
check bonami-query.log "type 33 .* 0 0 6000 $hostname\." \
    "dnssd -q printed the host service's SRV record"
check browse.log "^-;aroshost;IPv4;$share;$type;local" \
    "Share said goodbye on Ctrl-C"
check browse.log "^=;aroshost2;IPv4;ArosSvc;$type;local;arosbox\.local;192\.168\.167\.2;5004;" \
    "Two interfaces: on the second link the service has that link's address"
check browse.log "^=;aroshost2;IPv4;${share}b;$type;local;arosbox\.local;192\.168\.167\.9;8081;" \
    "An interface's address changed and mdnsd followed it"
check browse.log "^=;aroshost;IPv4;${share}b;$type;local;arosbox\.local;192\.168\.166\.2;8081;" \
    "The other interface kept its address"
check browse.log "^+;aroshost;IPv4;$ghost;$type;local" \
    "A task registered a service and ended without closing the library"
check browse.log "^-;aroshost;IPv4;$ghost;$type;local" \
    "mdnsd noticed the dead task and withdrew its service"
check browse.log "^=;aroshost;IPv4;$defend;$type;local;arosbox\.local;192\.168\.166\.2;5006;" \
    "$defend was announced under its own name"
check browse-late.log "^=;aroshost;IPv4;$defend;$type;local;arosbox\.local;192\.168\.166\.2;5006;\"updated=yes\"" \
    "UpdateServiceA changed a registered service's text"
check browse.log "^=;aroshost;IPv4;$defend.032.0402.041;$type;local;arosbox\.local;192\.168\.166\.2;5006;" \
    "Another host claimed the announced name and mdnsd moved to $defend (2)"
hostlabel=${hostname%%.*}
check bonami-envoy.log "envoy host=$hostlabel ip=192\.168\.16[67]\.1 entity=Filesystem service=Printer owner=Jaime" \
    "Envoy bridge: an inquiry for an entity found the host that announces it"
check bonami-envoy.log "envoy end" \
    "Envoy bridge: the inquiry ended with a last call"
check browse-envoy.log "^=;aroshost;IPv4;arosbox;_envoy-nipc._tcp;local;arosbox\.local;192\.168\.166\.2;376;.*entity=ArosEntity" \
    "Envoy bridge: Avahi resolved this host's Envoy publication"
if grep -q "entity=ArosEntity" "$work/bonami-envoy.log" 2>/dev/null; then
    echo "FAILED: Envoy bridge: the inquiry answered for a host without the entity"
    status=1
fi
echo "logs: $work"
exit $status
