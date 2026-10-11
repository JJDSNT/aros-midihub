#!/bin/sh
# Checks on Linux-hosted AROS what mdnsd does when another host probes for
# the name it is probing for at the same moment (RFC 6762 section 8.2).
#
#   tools/mdns-tiebreak-hosted.sh [AROS_DIR [WORK_DIR]]
#
# As tools/bonami-hosted.sh: eth1 for AROS (192.168.166.2), aroshost for
# Linux (192.168.166.1). AROS registers two services, ten seconds apart. The
# host listens to the mDNS group, and when it sees AROS's first probe for a
# name it sends a probe of its own for that name, as a second host starting
# at the same time would:
#
#   TieLose  with an SRV record that sorts after AROS's (port 65535). AROS
#            has to give way: wait a second and probe again from the start.
#   TieWin   with an SRV record that sorts before AROS's (port 1). AROS has
#            to carry on: three probes, then the announcement.
#
# The host never announces either name, so AROS gets both in the end.
# Exit status: 0 when AROS gave way for the first and not for the second.
set -eu

here=$(cd "$(dirname "$0")/.." && pwd)
aros=$(realpath "${1:-$here/build-aros-linux/bin/linux-x86_64/AROS}")
work=$(realpath "${2:-$(mktemp -d)}")
sys="$work/AROS"
type=_bonamitie._udp

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
cat > "$sys/S/tie-run" <<EOF
FailAt 21
CD SYS:System/Network/AROSTCP
Path C S ADD QUIET
Run >NIL: QUIET AROSTCP
WaitForPort AROSTCP
Wait 8
Run >NIL: $c/BonamiProbe REGISTER TieLose TYPE $type PORT 5004 SECONDS 20
Wait 10
Run >NIL: $c/BonamiProbe REGISTER TieWin TYPE $type PORT 5004 SECONDS 12
Wait 14
Shutdown
EOF
printf 'Run >NIL: Execute S:tie-run\n' > "$sys/S/User-Startup"

# The second host. It needs the group's traffic on aroshost and port 5353,
# which Avahi shares.
sudo timeout 70 python3 - "$work/tie.log" "$type" <<'PYEOF' &
import socket, struct, sys, time

GROUP, PORT, HOST = '224.0.0.251', 5353, '192.168.166.1'
log = open(sys.argv[1], 'w')
kind = sys.argv[2]

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

rx = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
rx.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
rx.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
rx.bind(('', PORT))
rx.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP,
              socket.inet_aton(GROUP) + socket.inet_aton(HOST))
rx.settimeout(1)
tx = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
tx.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_IF, socket.inet_aton(HOST))
tx.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 255)
tx.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_LOOP, 0)

answered = set()
end = time.time() + 60
while time.time() < end:
    try:
        data, source = rx.recvfrom(9000)
    except socket.timeout:
        continue
    if source[0] != '192.168.166.2' or len(data) < 12:
        continue
    ident, flags, qd, an, ns, ar = struct.unpack('>HHHHHH', data[:12])
    offset = 12
    try:
        questions = []
        for _ in range(qd):
            name, offset = read_name(data, offset)
            questions.append(name)
            offset += 4
        records = []
        for _ in range(an + ns + ar):
            name, offset = read_name(data, offset)
            rtype, rclass, ttl, rdlen = struct.unpack('>HHIH', data[offset:offset + 10])
            offset += 10 + rdlen
            records.append((name, rtype, ttl))
    except (IndexError, struct.error):
        continue
    now = time.time()
    for label in ('TieLose', 'TieWin'):
        full = '%s.%s.local' % (label, kind)
        if not flags & 0x8000 and ns and full in questions:
            log.write('%f probe %s\n' % (now, label))
            if label not in answered:
                answered.add(label)
                # a probe of our own for the same name, as a second host
                port = 65535 if label == 'TieLose' else 1
                rdata = struct.pack('>HHH', 0, 0, port) + wire('other.local')
                packet = struct.pack('>HHHHHH', 0, 0, 1, 0, 1, 0) + wire(full) + \
                    struct.pack('>HH', 255, 1) + wire(full) + \
                    struct.pack('>HHIH', 33, 1, 120, len(rdata)) + rdata
                tx.sendto(packet, (GROUP, PORT))
                log.write('%f competed %s port %d\n' % (time.time(), label, port))
        if flags & 0x8000 and any(n == full and t == 33 and ttl for n, t, ttl in records):
            log.write('%f announced %s\n' % (now, label))
    log.flush()
PYEOF

(cd "$sys" && timeout 80 xvfb-run -a boot/linux/AROSBootstrap > "$work/serial.log" 2>&1) || true
wait

status=0
result=$(python3 - "$work/tie.log" <<'PYEOF'
import sys
events = {}
for line in open(sys.argv[1]):
    when, what, label = line.split()[:3]
    events.setdefault(label, []).append((float(when), what))
for label in ('TieLose', 'TieWin'):
    seen = events.get(label, [])
    probes = [t for t, w in seen if w == 'probe']
    announced = [t for t, w in seen if w == 'announced']
    competed = [t for t, w in seen if w == 'competed']
    if not probes or not announced or not competed:
        print(label, 'incomplete', len(probes), 0)
        continue
    before = [t for t in probes if t < announced[0]]
    # how long after the competing probe the next probe of AROS came
    later = [t for t in before if t > competed[0]]
    gap = (later[0] - competed[0]) if later else 0
    print(label, 'ok', len(before), '%.2f' % (announced[0] - probes[0]), '%.2f' % gap)
PYEOF
)
echo "$result" > "$work/tie-result.log"
lose=$(echo "$result" | grep '^TieLose' || true)
win=$(echo "$result" | grep '^TieWin' || true)
set -- $lose
if [ "${2:-}" = ok ] && [ "$3" -ge 4 ] && awk "BEGIN{exit !($5 >= 0.9 && $4 >= 1.5)}"; then
    echo "ok: against a later record mdnsd gave way: $3 probes, the next one $5 s after the other host's, announced after $4 s"
else
    echo "FAIL: against a later record mdnsd did not give way ($lose)"
    status=1
fi
set -- $win
if [ "${2:-}" = ok ] && [ "$3" -eq 3 ] && awk "BEGIN{exit !($4 < 1.2)}"; then
    echo "ok: against an earlier record mdnsd carried on: $3 probes, announced after $4 s"
else
    echo "FAIL: against an earlier record mdnsd did not carry on ($win)"
    status=1
fi
echo "logs: $work"
exit $status
