#!/bin/sh
# Native bonami.library query/lookup smoke against an independently encoded
# Linux mDNS peer. The build directory must contain the proposed library,
# daemon and BonamiQueryProbe.
set -eu

image=$(realpath "$1")
work=$(realpath -m "$2")
build=$(realpath "$3")
sys="$work/AROS"
test ! -e "$sys"
mkdir -p "$work"
cp -a --reflink=auto "$image" "$sys"
mkdir -p "$sys/Extras/aros-midihub/C"
cp "$build/make-build/AROS/Libs/bonami.library" "$sys/Libs/bonami.library"
cp "$build/make-build/System/Network/Services/mdnsd" \
    "$sys/System/Network/Services/mdnsd"
cp "$build/BonamiQueryProbe" "$sys/Extras/aros-midihub/C/BonamiQueryProbe"

if ! ip link show aroshost >/dev/null 2>&1; then
    sudo ip link add eth1 type veth peer name aroshost
    sudo ip addr add 192.168.166.1/24 dev aroshost
fi
sudo ip link set eth1 up
sudo ip link set aroshost up
sudo ethtool -K aroshost tx off >/dev/null
sudo ethtool -K eth1 tx off >/dev/null
sudo setcap cap_net_raw,cap_net_admin+ep "$sys/boot/linux/AROSBootstrap"

db="$sys/System/Network/AROSTCP/db"
echo 'eth1 DEV=DEVS:networks/eth.device UNIT=1 IP=192.168.166.2 NETMASK=255.255.255.0 UP' >> "$db/interfaces"
printf 'HOST 192.168.166.2 arosbox.arosnet arosbox\nDOMAIN arosnet 192.168.166.\n' > "$db/netdb-myhost"
sed -i 's/OPENGUI=.*/OPENGUI=NO/' "$db/general.config"
cat > "$sys/S/query-run" <<'RUNEOF'
FailAt 21
CD SYS:System/Network/AROSTCP
Path C S ADD QUIET
Run >NIL: QUIET AROSTCP
WaitForPort AROSTCP
Wait 8
SYS:Extras/aros-midihub/C/BonamiQueryProbe >SYS:query.log
Wait 2
Shutdown
RUNEOF
printf 'Run >NIL: Execute S:query-run\n' > "$sys/S/User-Startup"

python3 -I - >"$work/peer.log" 2>&1 <<'PY' &
import socket
import struct
import time

def name(*labels):
    return b''.join(bytes([len(label)]) + label for label in labels) + b'\0'

def rr(owner, kind, value):
    return owner + struct.pack('!HHIH', kind, 0x8001, 120, len(value)) + value

query_type = name(b'_querytest', b'_udp', b'local')
query_instance = name(b'Query Peer', b'_querytest', b'_udp', b'local')
query_host = name(b'querypeer', b'local')
packet = struct.pack('!6H', 0, 0x8400, 0, 2, 0, 0)
packet += rr(query_type, 12, query_instance)
packet += rr(query_host, 1, socket.inet_aton('192.168.166.1'))

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
sock.bind(('0.0.0.0', 5353))
sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_IF,
                socket.inet_aton('192.168.166.1'))
sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 255)
end = time.monotonic() + 35
while time.monotonic() < end:
    sock.sendto(packet, ('224.0.0.251', 5353))
    time.sleep(.25)
print('independent query peer completed')
PY
peer=$!

sudo timeout 40 tcpdump -i aroshost -n -U -w "$work/mdns.pcap" \
    udp port 5353 >"$work/tcpdump.log" 2>&1 &
capture=$!
(cd "$sys" && timeout 35 xvfb-run -a boot/linux/AROSBootstrap \
    >"$work/serial.log" 2>&1) || true
wait "$peer"
wait "$capture" || test "$?" = 124
cp "$sys/query.log" "$work/query.log"
cat "$work/query.log"
grep -q 'BONAMI-QUERY: OK (0 failures' "$work/query.log"
if grep -Ei 'ALERT|Guru Meditation|Software Failure|UNHANDLED|SIGSEGV|tried to free' \
    "$work/serial.log"; then
    exit 1
fi
printf 'Native query ABI and independent-peer exchange passed; artifacts: %s\n' "$work"
