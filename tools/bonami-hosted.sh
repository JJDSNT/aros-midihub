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
# and aroshost for Linux (192.168.166.1). It uses sudo to create the pair, to
# turn off checksum offload on it (AROS would see UDP checksums the host never
# filled in) and to give the copied AROSBootstrap CAP_NET_RAW. The pair stays
# until the host restarts.
#
# mdnsd is started by the AROSTCP service manager. BonamiProbe registers
# ArosSvc, browses, and ends without withdrawing anything; the host publishes
# HostSvc and browses. The host also publishes ArosDup, a name a second
# BonamiProbe then asks for. Needs a running avahi-daemon, avahi-utils, ethtool and
# xvfb-run.
# Exit status: 0 when each side saw the other's service with its address and
# the host saw the AROS one leave, and AROS took another name for ArosDup.
set -eu

here=$(cd "$(dirname "$0")/.." && pwd)
aros=$(realpath "${1:-$here/build-aros-linux/bin/linux-x86_64/AROS}")
work=$(realpath "${2:-$(mktemp -d)}")
sys="$work/AROS"
type=_bonamiprobe._udp

test -x "$aros/boot/linux/AROSBootstrap"
test -f "$aros/Libs/bonami.library"
test -f "$aros/Extras/aros-midihub/C/BonamiProbe"
test -f "$aros/System/Network/Services/mdnsd"
test -f "$aros/System/Network/AROSTCP/db/services.d/mdns"
test -f "$aros/Devs/networks/eth.device" || test -f "$aros/Devs/Networks/eth.device"

if ! ip link show aroshost > /dev/null 2>&1; then
    sudo ip link add eth1 type veth peer name aroshost
    sudo ip addr add 192.168.166.1/24 dev aroshost
fi
sudo ip link set eth1 up
sudo ip link set aroshost up
# Linux must not answer on AROS's end of the pair with the same address.
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

# Not in the boot shell: see tools/camd-compat-hosted.sh.
cat > "$sys/S/bonami-run" <<EOF
FailAt 21
CD SYS:System/Network/AROSTCP
Path C S ADD QUIET
Run >NIL: QUIET AROSTCP
WaitForPort AROSTCP
Wait 3
Run >NIL: SYS:Extras/aros-midihub/C/BonamiProbe REGISTER ArosDup TYPE $type PORT 5005 SECONDS 12
SYS:Extras/aros-midihub/C/BonamiProbe REGISTER ArosSvc TYPE $type PORT 5004 BROWSE $type SECONDS 12 LEAK >SYS:bonami-probe.log
Wait 4
Shutdown
EOF
printf 'Run >NIL: Execute S:bonami-run\n' > "$sys/S/User-Startup"

(timeout 40 avahi-publish -s HostSvc "$type" 6000 k=v > "$work/publish.log" 2>&1 || true) &
(timeout 40 avahi-publish -s ArosDup "$type" 6001 > "$work/publish-dup.log" 2>&1 || true) &
(sleep 6; timeout 30 avahi-browse -rp "$type" > "$work/browse.log" 2>&1 || true) &
sleep 1
(cd "$sys" && timeout 60 xvfb-run -a boot/linux/AROSBootstrap \
    > "$work/serial.log" 2>&1) || true
wait
cp "$sys/bonami-probe.log" "$work/" 2>/dev/null || true

status=0
check() {
    if grep -q "$2" "$work/$1" 2>/dev/null; then
        echo "ok: $3"
    else
        echo "FAILED: $3"
        status=1
    fi
}
check bonami-probe.log 'add "HostSvc" .* host .*\.local 192\.168\.166\.1 port 6000' \
    "AROS resolved the host's service"
check browse.log "^=;aroshost;IPv4;ArosSvc;$type;local;arosbox\.local;192\.168\.166\.2;5004;" \
    "Avahi resolved the AROS service"
check browse.log "^-;aroshost;IPv4;ArosSvc;$type;local" \
    "Avahi saw the AROS service leave when its program closed the library"
# avahi-browse -p writes "ArosDup (2)" as ArosDup\032\0402\041.
check browse.log "^=;aroshost;IPv4;ArosDup.032.0402.041;$type;local;arosbox\.local;192\.168\.166\.2;5005;" \
    "AROS found the name ArosDup taken and announced ArosDup (2)"
echo "logs: $work"
exit $status
