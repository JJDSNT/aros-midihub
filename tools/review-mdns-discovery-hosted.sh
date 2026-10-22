#!/bin/sh
# Native ABI/packet smoke. Requires a prepared hosted image with the new
# library, daemon and Extras/aros-midihub/C/BonamiDiscoveryProbe installed.
# This uses a Linux peer with an independent DNS encoder, not another mdnsd.
set -eu
image=$(realpath "$1")
work=$(realpath -m "$2")
sys="$work/AROS"
test ! -e "$sys"
mkdir -p "$work"
cp -a --reflink=auto "$image" "$sys"
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
cat > "$sys/S/discovery-run" <<'RUNEOF'
FailAt 21
CD SYS:System/Network/AROSTCP
Path C S ADD QUIET
Run >NIL: QUIET AROSTCP
WaitForPort AROSTCP
Wait 8
SYS:Extras/aros-midihub/C/BonamiDiscoveryProbe >SYS:discovery.log
Wait 2
Shutdown
RUNEOF
printf 'Run >NIL: Execute S:discovery-run\n' > "$sys/S/User-Startup"
python3 -I - >"$work/peer.log" 2>&1 <<'PY' &
import socket, struct, time

def name(*labels):
    return b''.join(bytes([len(x)]) + x for x in labels) + b'\0'

def rr(owner, kind, value, shared=False):
    return owner + struct.pack('!HHIH', kind, 1 if shared else 0x8001, 60, len(value)) + value
kind = name(b'_midi2', b'_udp', b'local')
inst = name(b'Peer.With\\Slashes', b'_midi2', b'_udp', b'local')
host = name(b'peer', b'local')
text = b'umpendpointname=' + b'\\' * 98
product = b'ProductInstanceId=' + b'P' * 42
text = bytes([len(text)]) + text + bytes([len(product)]) + product
packet = struct.pack('!6H', 0, 0x8400, 0, 4, 0, 0)
packet += rr(kind, 12, inst, True)
packet += rr(inst, 33, struct.pack('!3H', 0, 0, 5504) + host)
packet += rr(inst, 16, text)
packet += rr(host, 1, socket.inet_aton('192.168.166.1'))
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(('0.0.0.0', 5353))
s.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_IF, socket.inet_aton('192.168.166.1'))
s.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 255)
end = time.monotonic() + 55
while time.monotonic() < end:
    s.sendto(packet, ('224.0.0.251', 5353))
    time.sleep(.5)
print('independent DNS-SD peer completed')
PY
peer=$!
sudo timeout 55 tcpdump -i aroshost -n -U -w "$work/mdns.pcap" udp port 5353 >"$work/tcpdump.log" 2>&1 &
capture=$!
(cd "$sys" && timeout 50 xvfb-run -a boot/linux/AROSBootstrap >"$work/serial.log" 2>&1) || true
wait "$peer"
wait "$capture" || test "$?" = 124
cp "$sys/discovery.log" "$work/discovery.log"
cat "$work/discovery.log"
grep -q 'BONAMI-DISCOVERY: OK (0 failures' "$work/discovery.log"
if grep -Ei 'ALERT|Guru Meditation|Software Failure|UNHANDLED|SIGSEGV|tried to free' "$work/serial.log"; then
    exit 1
fi
printf 'Native discovery ABI and independent-peer exchange passed; artifacts: %s\n' "$work"
