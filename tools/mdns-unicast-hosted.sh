#!/bin/sh
# Checks on Linux-hosted AROS when mdnsd answers the asker alone instead of
# the mDNS group (RFC 6762 sections 5.4 and 6.7).
#
#   tools/mdns-unicast-hosted.sh [AROS_DIR [WORK_DIR]]
#
# As tools/bonami-hosted.sh: eth1 for AROS (192.168.166.2, arosbox),
# aroshost for Linux (192.168.166.1). AROS registers two services. The host:
#
# 1. asks from another port than 5353, as a resolver that knows plain DNS
#    does, for the service type and for the host's address. The answer has
#    to come back to that port alone, with the question's ID and the
#    question in it, no record living longer than ten seconds, and no
#    cache-flush bit; nothing goes to the group.
# 2. asks from port 5353 with the unicast-response bit, a few seconds after
#    the service was announced: the answer comes to the host's own address.
# 3. waits until the group has not had the records for more than 30
#    seconds and asks the same way: now the answer goes to the group.
# 4. asks once more right away: to the host's own address again.
# Exit status: 0 when all four went that way.
set -eu

here=$(cd "$(dirname "$0")/.." && pwd)
aros=$(realpath "${1:-$here/build-aros-linux/bin/linux-x86_64/AROS}")
work=$(realpath "${2:-$(mktemp -d)}")
sys="$work/AROS"
type=_bonamiuni._udp

test -x "$aros/boot/linux/AROSBootstrap"
test -f "$aros/System/Network/Services/mdnsd"
test -f "$aros/Extras/aros-midihub/C/BonamiProbe"

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
mkdir -p "$work"
cp -a --reflink=auto "$aros" "$sys"
db="$sys/System/Network/AROSTCP/db"
echo 'eth1 DEV=DEVS:networks/eth.device UNIT=1 IP=192.168.166.2 NETMASK=255.255.255.0 UP' \
    >> "$db/interfaces"
printf 'HOST 192.168.166.2 arosbox.arosnet arosbox\nDOMAIN arosnet 192.168.166.\n' \
    > "$db/netdb-myhost"
sed -i 's/OPENGUI=.*/OPENGUI=NO/' "$db/general.config"
sudo setcap cap_net_raw,cap_net_admin+ep "$sys/boot/linux/AROSBootstrap"

c=SYS:Extras/aros-midihub/C
cat > "$sys/S/uni-run" <<RUNEOF
FailAt 21
CD SYS:System/Network/AROSTCP
Path C S ADD QUIET
Run >NIL: QUIET AROSTCP
WaitForPort AROSTCP
Wait 6
Run >NIL: $c/BonamiProbe REGISTER UniSvc TYPE $type PORT 5004 SECONDS 70
Run >NIL: $c/BonamiProbe REGISTER UniSvc2 TYPE $type PORT 5005 SECONDS 70
Echo "registered" >SYS:uni-registered.log
Wait 66
Shutdown
RUNEOF
printf 'Run >NIL: Execute S:uni-run\n' > "$sys/S/User-Startup"

(
    waited=0
    while [ ! -f "$sys/uni-registered.log" ] && [ $waited -lt 60 ]; do
        sleep 1
        waited=$((waited + 1))
    done
    sleep 5
    sudo python3 - "$work/uni.log" "$type" <<'PYEOF' || true
import socket, struct, sys, time

GROUP, PORT, HOST, AROS = '224.0.0.251', 5353, '192.168.166.1', '192.168.166.2'
log = open(sys.argv[1], 'w')
kind = sys.argv[2] + '.local'

def wire(name):
    return b''.join(bytes([len(l)]) + l.encode() for l in name.split('.')) + b'\0'

def read_name(data, offset):
    labels, jumped, end = [], False, offset
    while True:
        length = data[offset]
        if length == 0:
            offset += 1
            break
        if length & 0xc0 == 0xc0:
            if not jumped:
                end = offset + 2
            offset = ((length & 0x3f) << 8) | data[offset + 1]
            jumped = True
            continue
        labels.append(data[offset + 1:offset + 1 + length].decode('latin-1'))
        offset += 1 + length
    return '.'.join(labels), (end if jumped else offset)

def parse(data):
    ident, flags, qd, an, ns, ar = struct.unpack('>HHHHHH', data[:12])
    offset, questions, records = 12, [], []
    for _ in range(qd):
        name, offset = read_name(data, offset)
        questions.append((name.lower(),) + struct.unpack('>HH', data[offset:offset + 4]))
        offset += 4
    for _ in range(an + ns + ar):
        name, offset = read_name(data, offset)
        rtype, rclass, ttl, rdlen = struct.unpack('>HHIH', data[offset:offset + 10])
        records.append((name.lower(), rtype, rclass, ttl, data[offset + 10:offset + 10 + rdlen]))
        offset += 10 + rdlen
    return ident, flags, questions, records

# the group, on port 5353, telling where each packet was addressed
group = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
group.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
group.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
group.setsockopt(socket.IPPROTO_IP, socket.IP_PKTINFO, 1)
group.bind(('', PORT))
group.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP,
                 socket.inet_aton(GROUP) + socket.inet_aton(HOST))
group.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_IF, socket.inet_aton(HOST))
group.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 255)
group.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_LOOP, 0)

def from_group(seconds, wanted):
    """Responses from AROS that hold a record named wanted: where they were
    addressed."""
    seen, end = [], time.time() + seconds
    while True:
        left = end - time.time()
        if left <= 0:
            return seen
        group.settimeout(left)
        try:
            data, ancillary, _, source = group.recvmsg(9000, 256)
        except socket.timeout:
            return seen
        if source[0] != AROS or len(data) < 12:
            continue
        try:
            ident, flags, questions, records = parse(data)
        except (IndexError, struct.error):
            continue
        if not flags & 0x8000 or not any(r[0] == wanted for r in records):
            continue
        to = '?'
        for level, kind_, value in ancillary:
            if level == socket.IPPROTO_IP and kind_ == socket.IP_PKTINFO:
                to = socket.inet_ntoa(value[8:12])
        seen.append(to)

def drain():
    group.settimeout(0.01)
    try:
        while True:
            group.recvmsg(9000, 256)
    except socket.timeout:
        pass

# 1. a resolver that knows plain DNS
plain = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
plain.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_IF, socket.inet_aton(HOST))
plain.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_LOOP, 0)
plain.bind((HOST, 0))
for label, name, qtype, ident in (('legacy-ptr', kind, 12, 0x1234), ('legacy-a', 'arosbox.local', 1, 0x4321)):
    drain()
    plain.sendto(struct.pack('>HHHHHH', ident, 0x0100, 1, 0, 0, 0) + wire(name) + struct.pack('>HH', qtype, 1),
                 (GROUP, PORT))
    plain.settimeout(1)
    result = 'none'
    packets = 0
    try:
        deadline = time.time() + 1
        while time.time() < deadline:
            data, source = plain.recvfrom(9000)
            rid, flags, questions, records = parse(data)
            if not flags & 0x8000:
                continue
            packets += 1
            if result == 'none':
                result = 'id=%d qr=1 question=%d answers=%d maxttl=%d flush=%d from=%s:%d' % (
                    int(rid == ident), int(questions == [(name.lower(), qtype, 1)]),
                    sum(1 for r in records if r[0] == name.lower() and r[1] == qtype),
                    max([r[3] for r in records] or [0]),
                    sum(1 for r in records if r[2] & 0x8000), source[0], source[1])
            plain.settimeout(max(0.01, deadline - time.time()))
    except socket.timeout:
        pass
    except (IndexError, struct.error):
        result = 'unreadable'
    log.write('%s %s packets=%d group=%d\n' %
              (label, result, packets, len(from_group(0.4, name.lower()))))
    log.flush()

def ask_direct(label):
    drain()
    group.sendto(struct.pack('>HHHHHH', 0, 0, 1, 0, 0, 0) + wire(kind) + struct.pack('>HH', 12, 0x8001),
                 (GROUP, PORT))
    log.write('%s %s\n' % (label, ','.join(from_group(0.6, kind)) or 'none'))
    log.flush()

# 2. the unicast-response bit, soon after the announcement
ask_direct('direct-soon')
# 3. after the group has gone 30 seconds without the records
time.sleep(34)
ask_direct('direct-late')
# 4. and once more
time.sleep(0.5)
ask_direct('direct-again')
PYEOF
) &

(cd "$sys" && timeout 110 xvfb-run -a boot/linux/AROSBootstrap > "$work/serial.log" 2>&1) || true
wait

status=0
check() {
    if [ "$1" = yes ]; then echo "ok: $2"; else echo "FAIL: $2"; status=1; fi
}
has() { grep -q "$1" "$work/uni.log" && echo yes || echo no; }
check "$(has '^legacy-ptr id=1 qr=1 question=1 answers=2 maxttl=10 flush=0 from=192.168.166.2:5353 packets=1 group=0$')" \
    "a plain DNS question gathers both services in one reply to its port: same ID, the question, ten seconds, no cache-flush bit"
check "$(has '^legacy-a id=1 qr=1 question=1 answers=1 maxttl=10 flush=0 from=192.168.166.2:5353 packets=1 group=0$')" \
    "and so is one for the host's address"
check "$(has '^direct-soon 192.168.166.1,192.168.166.1$')" "a question with the unicast-response bit, soon after the announcement, answers both services to the asker alone"
check "$(has '^direct-late 224.0.0.251$')" "after 34 seconds without the records on the group, it is answered to the group"
check "$(has '^direct-again 192.168.166.1,192.168.166.1$')" "and right after that both services go to the asker alone again"
echo "logs: $work"
exit $status
