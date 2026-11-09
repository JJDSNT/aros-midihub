#!/bin/sh
# Verify IPv6 mDNS receive-link isolation on two Linux-hosted AROS links.
# A legacy AAAA question enters only eth1. The unicast answer must return on
# eth1 only, even though AROSTCP delivers multicast UDP to every reused socket;
# mdnsd selects the real input link from IPV6_PKTINFO.
set -eu

if test "$#" -ne 3; then
    echo "usage: $0 AROS_DIR WORK_DIR BUILD_DIR" >&2
    exit 2
fi

image=$(realpath "$1")
work=$(realpath -m "$2")
build=$(realpath "$3")
sys="$work/AROS"

if test -f "$build/bin/linux-x86_64/AROS/Libs/bonami.library"; then
    bonami="$build/bin/linux-x86_64/AROS/Libs/bonami.library"
    mdnsd="$build/bin/linux-x86_64/AROS/System/Network/Services/mdnsd"
else
    bonami="$build/make-build/AROS/Libs/bonami.library"
    mdnsd="$build/make-build/System/Network/Services/mdnsd"
fi
test -x "$image/boot/linux/AROSBootstrap"
test -f "$bonami"
test -f "$mdnsd"
test ! -e "$sys"

mkdir -p "$work"
cp -a --reflink=auto "$image" "$sys"
cp "$bonami" "$sys/Libs/bonami.library"
cp "$mdnsd" "$sys/System/Network/Services/mdnsd"

link() {
    if ! ip link show "$2" >/dev/null 2>&1; then
        sudo ip link add "$1" type veth peer name "$2"
    fi
    sudo ip link set "$1" up
    sudo ip link set "$2" up
    sudo sysctl -qw "net.ipv6.conf.$1.disable_ipv6=1"
    sudo sysctl -qw "net.ipv6.conf.$2.disable_ipv6=0"
    sudo ip -6 addr flush dev "$2" scope link
    sudo ip -6 addr add fe80::1/64 dev "$2" nodad
    sudo ethtool -K "$1" tx off >/dev/null
    sudo ethtool -K "$2" tx off >/dev/null
}
link eth1 aroshost
link eth2 aroshost2
sudo setcap cap_net_raw,cap_net_admin+ep "$sys/boot/linux/AROSBootstrap"

db="$sys/System/Network/AROSTCP/db"
echo 'eth1 DEV=DEVS:networks/eth.device UNIT=1 IP6=fe80::2 PREFIXLEN=64 UP' \
    >> "$db/interfaces"
echo 'eth2 DEV=DEVS:networks/eth.device UNIT=2 IP6=fe80::2 PREFIXLEN=64 UP' \
    >> "$db/interfaces"
printf 'HOST ::1 arosbox.arosnet arosbox\n' > "$db/netdb-myhost"
sed -i 's/OPENGUI=.*/OPENGUI=NO/' "$db/general.config"

cat > "$sys/S/mdns-ipv6-links-run" <<'RUNEOF'
FailAt 21
CD SYS:System/Network/AROSTCP
Path C S ADD QUIET
Run >NIL: QUIET AROSTCP
WaitForPort AROSTCP
Wait 10
C:ifconfig -a >SYS:ifconfig6-links.log
Wait 18
Break `Status COMMAND=SYS:System/Network/Services/mdnsd` C
Wait 2
Shutdown
RUNEOF
printf 'Run >NIL: Execute S:mdns-ipv6-links-run\n' > "$sys/S/User-Startup"

python3 -I - aroshost aroshost2 >"$work/peer.log" 2>&1 <<'PY' &
import select
import socket
import struct
import sys
import time

first = socket.if_nametoindex(sys.argv[1])
second = socket.if_nametoindex(sys.argv[2])
ident = 0x6a71

def wire_name(text):
    return b''.join(bytes((len(label),)) + label.encode('ascii')
                    for label in text.split('.')) + b'\0'

question = struct.pack('!6H', ident, 0x0100, 1, 0, 0, 0)
question += wire_name('arosbox.local') + struct.pack('!HH', 28, 1)

sock = socket.socket(socket.AF_INET6, socket.SOCK_DGRAM)
sock.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_V6ONLY, 1)
sock.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_RECVPKTINFO, 1)
sock.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_MULTICAST_IF, first)
sock.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_MULTICAST_HOPS, 255)
sock.bind(('::', 0))
destination = ('ff02::fb', 5353, 0, first)

# Allow interface DAD, mdnsd probing and announcements to finish.
time.sleep(16)
deadline = time.monotonic() + 9
next_query = 0.0
answers = []
while time.monotonic() < deadline:
    now = time.monotonic()
    if now >= next_query:
        sock.sendto(question, destination)
        next_query = now + 0.5
    readable, _, _ = select.select((sock,), (), (), 0.2)
    if not readable:
        continue
    packet, ancillary, _, source = sock.recvmsg(9000, 256)
    if len(packet) < 12:
        continue
    received_id, flags, _, answer_count, _, _ = struct.unpack_from('!6H', packet)
    if received_id != ident or not flags & 0x8000 or answer_count == 0:
        continue
    incoming = 0
    for level, kind, data in ancillary:
        if level == socket.IPPROTO_IPV6 and kind == socket.IPV6_PKTINFO:
            incoming = struct.unpack_from('@I', data, 16)[0]
    answers.append((incoming, source[0]))

links = {item[0] for item in answers}
print('legacy AAAA answers:', len(answers))
print('answer input interface indexes:', sorted(links))
print('expected interface index:', first, 'forbidden:', second)
if not answers or links != {first}:
    raise SystemExit(1)
PY
peer=$!

(cd "$sys" && timeout 38 xvfb-run -a boot/linux/AROSBootstrap \
    >"$work/serial.log" 2>&1) || true
peer_status=0
wait "$peer" || peer_status=$?
cp "$sys/ifconfig6-links.log" "$work/ifconfig6-links.log" 2>/dev/null || true

status=$peer_status
if ! grep -q 'eth1' "$work/ifconfig6-links.log" 2>/dev/null ||
   ! grep -q 'eth2' "$work/ifconfig6-links.log" 2>/dev/null; then
    echo 'FAILED: AROS did not configure both IPv6 interfaces'
    status=1
fi
if grep -Eiq 'ALERT|Guru Meditation|Software Failure|UNHANDLED|SIGSEGV|tried to free' \
        "$work/serial.log"; then
    status=1
fi
cat "$work/peer.log"
if test "$status" -eq 0; then
    echo 'ok: IPv6 multicast receive and legacy reply stayed on their link'
else
    echo 'FAILED: IPv6 per-link isolation check failed'
fi
echo "artifacts: $work"
exit "$status"
