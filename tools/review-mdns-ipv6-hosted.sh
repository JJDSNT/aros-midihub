#!/bin/sh
# Exercise the proposed dual-stack mdnsd on a real Linux-hosted AROS image.
#
#   tools/review-mdns-ipv6-hosted.sh AROS_DIR WORK_DIR BUILD_DIR [dual]
#
# AROS_DIR must contain an IPv6-capable AROSTCP image. BUILD_DIR must be an
# AROS build directory containing the bonami.library and mdnsd produced from
# the proposal (the normal bin/linux-x86_64 layout or the review make-build
# layout).
# The image is copied to WORK_DIR, so the original is not modified.
#
# The Linux end of the veth pair is fe80::1 on aroshost. The kernel's IPv6
# stack is disabled on eth1 because that end belongs to hosted AROS. AROSTCP
# configures eth1 with only a static link-local address, waits for DAD, and
# starts mdnsd. (IP6=AUTO currently does not initialise an IPv6-only SANA-II
# device; that AROSTCP defect is tracked separately.) An
# independently encoded Linux peer then checks both directions:
#
# - AROS answers an AAAA query for its .local host over ff02::fb;
# - dnssd on AROS asks for peer.local AAAA and receives fe80::1;
# - every IPv6 mDNS datagram received from AROS has hop limit 255.
set -eu

if test "$#" -lt 3 || test "$#" -gt 4; then
    echo "usage: $0 AROS_DIR WORK_DIR BUILD_DIR [dual]" >&2
    exit 2
fi

image=$(realpath "$1")
work=$(realpath -m "$2")
build=$(realpath "$3")
mode=${4:-ipv6}
test "$mode" = ipv6 || test "$mode" = dual
sys="$work/AROS"
peer_if=aroshost
aros_if=eth1

test -x "$image/boot/linux/AROSBootstrap"
test -f "$image/C/dnssd"
test -f "$image/Devs/networks/eth.device" || \
    test -f "$image/Devs/Networks/eth.device"
if test -f "$build/bin/linux-x86_64/AROS/Libs/bonami.library"; then
    bonami="$build/bin/linux-x86_64/AROS/Libs/bonami.library"
    mdnsd="$build/bin/linux-x86_64/AROS/System/Network/Services/mdnsd"
else
    bonami="$build/make-build/AROS/Libs/bonami.library"
    mdnsd="$build/make-build/System/Network/Services/mdnsd"
fi
test -f "$bonami"
test -f "$mdnsd"
test ! -e "$sys"

mkdir -p "$work"
cp -a --reflink=auto "$image" "$sys"
cp "$bonami" "$sys/Libs/bonami.library"
cp "$mdnsd" "$sys/System/Network/Services/mdnsd"

if ! ip link show "$peer_if" >/dev/null 2>&1; then
    sudo ip link add "$aros_if" type veth peer name "$peer_if"
fi
sudo ip link set "$aros_if" up
sudo ip link set "$peer_if" up
# Linux must not run a second IPv6 host on the end consumed by eth.device.
sudo sysctl -qw "net.ipv6.conf.$aros_if.disable_ipv6=1"
sudo sysctl -qw "net.ipv6.conf.$peer_if.disable_ipv6=0"
sudo ip -6 addr flush dev "$peer_if" scope link
sudo ip -6 addr add fe80::1/64 dev "$peer_if" nodad
if test "$mode" = dual; then
    sudo ip addr replace 192.168.166.1/24 dev "$peer_if"
fi
# Hosted eth.device sees frames before Linux has filled deferred checksums.
sudo ethtool -K "$peer_if" tx off >/dev/null
sudo ethtool -K "$aros_if" tx off >/dev/null
sudo setcap cap_net_raw,cap_net_admin+ep "$sys/boot/linux/AROSBootstrap"

db="$sys/System/Network/AROSTCP/db"
if test "$mode" = dual; then
    echo "$aros_if DEV=DEVS:networks/eth.device UNIT=1 IP=192.168.166.2 NETMASK=255.255.255.0 IP6=fe80::2 PREFIXLEN=64 UP" \
        >> "$db/interfaces"
else
    echo "$aros_if DEV=DEVS:networks/eth.device UNIT=1 IP6=fe80::2 PREFIXLEN=64 UP" \
        >> "$db/interfaces"
fi
# The first HOST entry supplies gethostname(); mdnsd obtains the actual
# interface address from SIOCGLIFADDR, not from this resolver entry.
printf 'HOST ::1 arosbox.arosnet arosbox\n' > "$db/netdb-myhost"
sed -i 's/OPENGUI=.*/OPENGUI=NO/' "$db/general.config"

cat > "$sys/S/mdns-ipv6-run" <<'RUNEOF'
FailAt 21
CD SYS:System/Network/AROSTCP
Path C S ADD QUIET
Run >NIL: QUIET AROSTCP
WaitForPort AROSTCP
Wait 10
C:ifconfig -a >SYS:ifconfig6.log
C:netstat -rn >SYS:routes6.log
Run >SYS:query6.log C:dnssd -q peer.local AAAA
Wait 18
Break `Status COMMAND=C:dnssd` C
Wait 2
Break `Status COMMAND=SYS:System/Network/Services/mdnsd` C
Wait 2
Shutdown
RUNEOF
printf 'Run >NIL: Execute S:mdns-ipv6-run\n' > "$sys/S/User-Startup"

python3 -I - "$peer_if" "$mode" >"$work/peer.log" 2>&1 <<'PY' &
import ipaddress
import select
import socket
import struct
import sys
import time

interface = sys.argv[1]
mode = sys.argv[2]
ifindex = socket.if_nametoindex(interface)
group = ('ff02::fb', 5353, 0, ifindex)


def wire_name(text):
    return b''.join(bytes((len(label),)) + label.encode('ascii')
                    for label in text.rstrip('.').split('.')) + b'\0'


def read_name(packet, offset):
    labels = []
    end = None
    seen = set()
    while True:
        if offset >= len(packet) or offset in seen:
            raise ValueError('bad DNS name')
        seen.add(offset)
        length = packet[offset]
        if length & 0xc0 == 0xc0:
            if offset + 1 >= len(packet):
                raise ValueError('short DNS pointer')
            if end is None:
                end = offset + 2
            offset = ((length & 0x3f) << 8) | packet[offset + 1]
            continue
        offset += 1
        if length == 0:
            return '.'.join(labels).lower(), end or offset
        if length > 63 or offset + length > len(packet):
            raise ValueError('bad DNS label')
        labels.append(packet[offset:offset + length].decode('ascii'))
        offset += length


def inspect(packet):
    if len(packet) < 12:
        return False, False, []
    ident, flags, qd, an, ns, ar = struct.unpack_from('!6H', packet)
    del ident
    offset = 12
    questions = []
    records = []
    for _ in range(qd):
        owner, offset = read_name(packet, offset)
        if offset + 4 > len(packet):
            raise ValueError('short question')
        kind, qclass = struct.unpack_from('!HH', packet, offset)
        offset += 4
        questions.append((owner, kind, qclass))
    for section, count in enumerate((an, ns, ar)):
        for _ in range(count):
            owner, offset = read_name(packet, offset)
            if offset + 10 > len(packet):
                raise ValueError('short record')
            kind, rclass, ttl, length = struct.unpack_from('!HHIH', packet,
                                                            offset)
            offset += 10
            if offset + length > len(packet):
                raise ValueError('short rdata')
            records.append((section, owner, kind, rclass, ttl,
                            packet[offset:offset + length]))
            offset += length
    asks_peer = any(owner == 'peer.local' and kind in (28, 255)
                    for owner, kind, qclass in questions)
    return bool(flags & 0x8000), asks_peer, records


peer_owner = wire_name('peer.local')
peer_data = socket.inet_pton(socket.AF_INET6, 'fe80::1')
peer_answer = struct.pack('!6H', 0, 0x8400, 0, 1, 0, 0)
peer_answer += peer_owner + struct.pack('!HHIH', 28, 0x8001, 120,
                                        len(peer_data)) + peer_data
host_question = struct.pack('!6H', 0, 0, 1, 0, 0, 0)
host_question += wire_name('arosbox.local') + struct.pack('!HH', 28, 1)
host_question4 = struct.pack('!6H', 0x4a71, 0x0100, 1, 0, 0, 0)
host_question4 += wire_name('arosbox.local') + struct.pack('!HH', 1, 1)

sock = socket.socket(socket.AF_INET6, socket.SOCK_DGRAM)
sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
if hasattr(socket, 'SO_REUSEPORT'):
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
sock.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_V6ONLY, 1)
sock.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_JOIN_GROUP,
                socket.inet_pton(socket.AF_INET6, 'ff02::fb') +
                struct.pack('@I', ifindex))
sock.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_MULTICAST_IF, ifindex)
sock.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_MULTICAST_HOPS, 255)
sock.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_MULTICAST_LOOP, 0)
sock.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_RECVHOPLIMIT, 1)
sock.bind(('::', 5353))

sock4 = None
if mode == 'dual':
    sock4 = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock4.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_IF,
                     socket.inet_aton('192.168.166.1'))
    sock4.bind(('192.168.166.1', 0))

deadline = time.monotonic() + 38
next_question = time.monotonic()
next_question4 = next_question
peer_queries = 0
aros_answers = 0
aros_answers4 = 0
bad_hops = []
sources = set()
while time.monotonic() < deadline:
    now = time.monotonic()
    if now >= next_question:
        sock.sendto(host_question, group)
        next_question = now + 0.5
    if sock4 is not None and now >= next_question4:
        sock4.sendto(host_question4, ('224.0.0.251', 5353))
        next_question4 = now + 0.5
    sockets = (sock,) if sock4 is None else (sock, sock4)
    readable, _, _ = select.select(sockets, (), (), 0.1)
    if not readable:
        continue
    if sock4 is not None and sock4 in readable:
        packet, source = sock4.recvfrom(9000)
        del source
        try:
            is_response, _, records = inspect(packet)
        except (UnicodeDecodeError, ValueError, struct.error):
            records = []
            is_response = False
        if is_response and struct.unpack_from('!H', packet)[0] == 0x4a71 and any(
                section == 0 and owner.startswith('arosbox') and
                owner.endswith('.local') and kind == 1 and
                data == socket.inet_aton('192.168.166.2')
                for section, owner, kind, rclass, ttl, data in records):
            aros_answers4 += 1
        if sock not in readable:
            continue
    packet, ancillary, msg_flags, source = sock.recvmsg(9000, 256)
    del msg_flags
    if source[0] == 'fe80::1':
        continue
    hop = None
    for level, kind, data in ancillary:
        if level == socket.IPPROTO_IPV6 and kind == socket.IPV6_HOPLIMIT:
            hop = struct.unpack('i', data[:4])[0]
    sources.add(source[0])
    if hop != 255:
        bad_hops.append((source[0], hop))
    try:
        is_response, asks_peer, records = inspect(packet)
    except (UnicodeDecodeError, ValueError, struct.error):
        continue
    if asks_peer and not is_response:
        peer_queries += 1
        sock.sendto(peer_answer, group)
    if is_response and any(section == 0 and owner.startswith('arosbox') and
            owner.endswith('.local') and kind == 28 and len(data) == 16 and
            ipaddress.IPv6Address(data).is_link_local
            for section, owner, kind, rclass, ttl, data in records):
        aros_answers += 1

print('AROS IPv6 sources:', ', '.join(sorted(sources)) or '(none)')
print('AROS AAAA queries for peer.local:', peer_queries)
print('AROS AAAA responses for arosbox.local:', aros_answers)
if mode == 'dual':
    print('AROS A responses for arosbox.local:', aros_answers4)
print('AROS packets with hop limit other than 255:',
      sorted(set(bad_hops)))
if (peer_queries < 1 or aros_answers < 1 or bad_hops or
        (mode == 'dual' and aros_answers4 < 1)):
    raise SystemExit(1)
PY
peer=$!

sudo timeout 45 tcpdump -i "$peer_if" -n -vv -U -w "$work/mdns-ipv6.pcap" \
    'ip6 or ip' >"$work/tcpdump.log" 2>&1 &
capture=$!
(cd "$sys" && timeout 42 xvfb-run -a boot/linux/AROSBootstrap \
    >"$work/serial.log" 2>&1) || true
peer_status=0
wait "$peer" || peer_status=$?
wait "$capture" || test "$?" = 124
cp "$sys/query6.log" "$work/query6.log" 2>/dev/null || true
cp "$sys/ifconfig6.log" "$work/ifconfig6.log" 2>/dev/null || true
cp "$sys/routes6.log" "$work/routes6.log" 2>/dev/null || true

status=$peer_status
if grep -Eiq 'peer\.local.*type 28.*fe800000000000000000000000000001' \
        "$work/query6.log" 2>/dev/null; then
    echo 'ok: AROS queried and received the Linux peer AAAA record'
else
    echo 'FAILED: AROS did not report the Linux peer AAAA record'
    status=1
fi
if grep -Ei 'ALERT|Guru Meditation|Software Failure|UNHANDLED|SIGSEGV|tried to free' \
        "$work/serial.log"; then
    status=1
fi
cat "$work/peer.log"
if test "$peer_status" -eq 0; then
    echo "ok: $mode multicast exchange and hop-limit checks passed"
else
    echo 'FAILED: IPv6 peer checks failed'
fi
echo "artifacts: $work"
exit "$status"
