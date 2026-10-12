#!/bin/sh
# Checks on Linux-hosted AROS the mDNS responder alone: an mdnsd that only
# answers for the host's name, as in the first cut proposed for the AROS
# tree (workbench/network/mdns). No bonami.library is needed.
#
#   tools/mdnsd-responder-hosted.sh [AROS_DIR [WORK_DIR]]
#
# eth1 for AROS (192.168.166.2, host name arosbox), aroshost for Linux
# (192.168.166.1). The Linux side listens to the mDNS group and plays the
# other host. Five boots:
#
# plain   nobody else has the name: three probes, the address announced
#         twice, Avahi resolves arosbox.local, and a goodbye when the stack
#         stops.
# taken   another host answers the first probe: AROS takes arosbox-2.local.
# late    another host claims the name after AROS announced it: AROS gives
#         it up and takes arosbox-2.local.
# tie     another host probes at the same moment with a later address: AROS
#         gives way for a second, probes again, and gets the name.
# ask     a plain DNS question from another port is answered to that port
#         alone; a question with the unicast-response bit is answered to
#         the asker soon after the announcement and to the group once the
#         group has gone 30 seconds without the record.
# Exit status: 0 when all went that way.
set -eu

here=$(cd "$(dirname "$0")/.." && pwd)
aros=$(realpath "${1:-$here/build-aros-linux/bin/linux-x86_64/AROS}")
work=$(realpath "${2:-$(mktemp -d)}")
sys="$work/AROS"

test -x "$aros/boot/linux/AROSBootstrap"
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

# boot NAME MODE SECONDS: what the Linux side does. 0 listens, 1 answers the
# first probe, 2 claims the name after the second announcement, 3 probes at
# the same time, 4 asks questions.
boot() {
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
    cat > "$sys/S/responder-run" <<RUNEOF
FailAt 21
CD SYS:System/Network/AROSTCP
Path C S ADD QUIET
Run >NIL: QUIET AROSTCP
WaitForPort AROSTCP
Wait $3
Break \`Status COMMAND=AROSTCP\` C
Wait 4
Shutdown
RUNEOF
    printf 'Run >NIL: Execute S:responder-run\n' > "$sys/S/User-Startup"

    sudo timeout $(($3 + 40)) python3 - "$work/host-$1.log" "$2" "$(($3 + 30))" <<'PYEOF' &
import socket, struct, sys, time

GROUP, PORT, HOST, AROS = '224.0.0.251', 5353, '192.168.166.1', '192.168.166.2'
log = open(sys.argv[1], 'w')
mode = int(sys.argv[2])
end = time.time() + int(sys.argv[3])

def wire(name):
    return b''.join(bytes([len(l)]) + l.encode() for l in name.split('.')) + b'\0'

def read_name(data, offset):
    labels, jumped, stop = [], False, offset
    while True:
        length = data[offset]
        if length == 0:
            offset += 1
            break
        if length & 0xc0 == 0xc0:
            if not jumped:
                stop = offset + 2
            offset = ((length & 0x3f) << 8) | data[offset + 1]
            jumped = True
            continue
        labels.append(data[offset + 1:offset + 1 + length].decode('latin-1'))
        offset += 1 + length
    return '.'.join(labels), (stop if jumped else offset)

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
    return ident, flags, questions, records, ns

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
group.settimeout(0.2)

plain = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
plain.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_IF, socket.inet_aton(HOST))
plain.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_LOOP, 0)
plain.bind((HOST, 0))

MINE = socket.inet_aton(AROS)
acted = False
announcements = 0
act_at = None
asked = []          # (label, time) of questions with the unicast-response bit
plan = []           # (time, label) still to ask

def claim(name):
    packet = struct.pack('>HHHHHH', 0, 0x8400, 0, 1, 0, 0) + wire(name) + \
        struct.pack('>HHIH', 1, 0x8001, 120, 4) + socket.inet_aton('192.168.166.77')
    group.sendto(packet, (GROUP, PORT))
    log.write('%f claimed %s\n' % (time.time(), name))

def ask_plain(label, ident):
    plain.sendto(struct.pack('>HHHHHH', ident, 0x0100, 1, 0, 0, 0) + wire('arosbox.local') +
                 struct.pack('>HH', 1, 1), (GROUP, PORT))
    plain.settimeout(1)
    result = 'none'
    try:
        data, source = plain.recvfrom(9000)
        rid, flags, questions, records, _ = parse(data)
        result = 'id=%d qr=%d question=%d answers=%d maxttl=%d flush=%d from=%s:%d' % (
            int(rid == ident), int(bool(flags & 0x8000)),
            int(questions == [('arosbox.local', 1, 1)]),
            sum(1 for r in records if r[0] == 'arosbox.local' and r[1] == 1 and r[4] == MINE),
            max([r[3] for r in records] or [0]), sum(1 for r in records if r[2] & 0x8000),
            source[0], source[1])
    except socket.timeout:
        pass
    log.write('%f %s %s\n' % (time.time(), label, result))

while time.time() < end:
    now = time.time()
    if act_at is not None and not acted and now >= act_at:
        acted = True
        if mode == 2:
            claim('arosbox.local')
        elif mode == 4:
            ask_plain('legacy', 0x2b2b)
            plan = [(time.time() + 0.5, 'direct-soon'), (time.time() + 35, 'direct-late'),
                    (time.time() + 36, 'direct-again')]
    if plan and now >= plan[0][0]:
        _, label = plan.pop(0)
        group.sendto(struct.pack('>HHHHHH', 0, 0, 1, 0, 0, 0) + wire('arosbox.local') +
                     struct.pack('>HH', 1, 0x8001), (GROUP, PORT))
        asked.append((label, time.time()))
    try:
        data, ancillary, _, source = group.recvmsg(9000, 256)
    except socket.timeout:
        continue
    if source[0] != AROS or len(data) < 12:
        continue
    try:
        ident, flags, questions, records, ns = parse(data)
    except (IndexError, struct.error):
        continue
    now = time.time()
    to = '?'
    for level, kind, value in ancillary:
        if level == socket.IPPROTO_IP and kind == socket.IP_PKTINFO:
            to = socket.inet_ntoa(value[8:12])
    response = bool(flags & 0x8000)
    for host in ('arosbox.local', 'arosbox-2.local'):
        if not response and ns and any(q[0] == host for q in questions):
            log.write('%f probe %s\n' % (now, host))
            if host == 'arosbox.local' and not acted and mode == 1:
                acted = True
                claim(host)
            if host == 'arosbox.local' and not acted and mode == 3:
                acted = True
                # a probe of our own for the same name; 192.168.166.200
                # sorts after 192.168.166.2
                packet = struct.pack('>HHHHHH', 0, 0, 1, 0, 1, 0) + wire(host) + \
                    struct.pack('>HH', 255, 1) + wire(host) + \
                    struct.pack('>HHIH', 1, 1, 120, 4) + socket.inet_aton('192.168.166.200')
                group.sendto(packet, (GROUP, PORT))
                log.write('%f competed %s\n' % (time.time(), host))
        if response:
            for name, rtype, rclass, ttl, rdata in records:
                if name != host or rtype != 1 or rdata != MINE:
                    continue
                if ttl == 0:
                    log.write('%f goodbye %s\n' % (now, host))
                    continue
                if asked and now - asked[-1][1] < 0.6:
                    log.write('%f %s %s\n' % (now, asked[-1][0], to))
                    continue
                log.write('%f address %s\n' % (now, host))
                if host == 'arosbox.local':
                    announcements += 1
                    if announcements == 2 and mode in (2, 4) and act_at is None:
                        act_at = now + 1.5
    log.flush()
PYEOF

    (sleep 16; for name in arosbox.local arosbox-2.local; do
        out=$(timeout 4 avahi-resolve -4 -n "$name" 2> /dev/null || true)
        echo "$name ${out:--}"
    done > "$work/resolve-$1.log") &

    (cd "$sys" && timeout $(($3 + 50)) xvfb-run -a boot/linux/AROSBootstrap > "$work/serial-$1.log" 2>&1) || true
    wait
}

count() { grep -c " $2 $3\$" "$work/host-$1.log" || true; }
resolved() { grep "^$2 " "$work/resolve-$1.log" | grep -c '192\.168\.166\.2' || true; }
has() { grep -q "$2" "$work/host-$1.log" && echo yes || echo no; }

status=0
check() {
    if [ "$1" = yes ]; then echo "ok: $2"; else echo "FAIL: $2"; status=1; fi
}
yes_if() { if "$@"; then echo yes; else echo no; fi; }

boot plain 0 22
p=$(count plain probe arosbox.local)
check "$(yes_if [ "$p" -eq 3 ])" "plain: three probes for arosbox.local ($p)"
check "$(yes_if [ "$(count plain address arosbox.local)" -eq 2 ])" "plain: the address is announced twice"
check "$(yes_if [ "$(count plain probe arosbox-2.local)" -eq 0 ])" "plain: no other name is tried"
check "$(yes_if [ "$(resolved plain arosbox.local)" -eq 1 ])" "plain: Avahi resolves arosbox.local to AROS"
check "$(yes_if [ "$(count plain goodbye arosbox.local)" -ge 1 ])" "plain: a goodbye for the name when the stack stops"

boot taken 1 22
check "$(yes_if [ "$(count taken claimed arosbox.local)" -eq 1 ])" "taken: another host answers the first probe"
p=$(count taken probe arosbox-2.local)
check "$(yes_if [ "$p" -eq 3 ])" "taken: AROS probes for arosbox-2.local instead ($p probes)"
check "$(yes_if [ "$(count taken address arosbox-2.local)" -eq 2 ])" "taken: and announces its address under that name"
check "$(yes_if [ "$(count taken address arosbox.local)" -eq 0 ])" "taken: never under the name the other host has"
check "$(yes_if [ "$(resolved taken arosbox-2.local)" -eq 1 ])" "taken: Avahi resolves arosbox-2.local to AROS"

boot late 2 22
check "$(yes_if [ "$(count late address arosbox.local)" -eq 2 ])" "late: AROS announces arosbox.local"
check "$(yes_if [ "$(count late claimed arosbox.local)" -eq 1 ])" "late: then another host says the name is its own"
p=$(count late probe arosbox-2.local)
check "$(yes_if [ "$p" -eq 3 ])" "late: AROS gives the name up and probes for arosbox-2.local ($p probes)"
check "$(yes_if [ "$(count late address arosbox-2.local)" -eq 2 ])" "late: and announces its address under that name"

boot tie 3 22
gap=$(python3 - "$work/host-tie.log" <<'PYEOF'
import sys
competed, probes, announced = None, [], None
for line in open(sys.argv[1]):
    when, what, name = line.split()[:3]
    when = float(when)
    if what == 'competed':
        competed = when
    elif what == 'probe' and name == 'arosbox.local':
        probes.append(when)
    elif what == 'address' and name == 'arosbox.local' and announced is None:
        announced = when
before = [t for t in probes if announced is None or t < announced]
later = [t for t in before if competed is not None and t > competed]
print(len(before), '%.2f' % ((later[0] - competed) if later else 0), 1 if announced else 0)
PYEOF
)
set -- $gap
check "$(yes_if [ "$(count tie competed arosbox.local)" -eq 1 ])" "tie: another host probes at the same time, with a later address"
check "$(yes_if awk "BEGIN{exit !(${1:-0} >= 4 && ${2:-0} >= 0.9)}")" "tie: AROS gives way: $1 probes in all, the next one $2 s after the other host's"
check "$(yes_if [ "${3:-0}" -eq 1 ])" "tie: and has arosbox.local in the end"
check "$(yes_if [ "$(count tie probe arosbox-2.local)" -eq 0 ])" "tie: without trying another name"

boot ask 4 52
check "$(has ask ' legacy id=1 qr=1 question=1 answers=1 maxttl=10 flush=0 from=192.168.166.2:5353$')" \
    "ask: a plain DNS question is answered to its port: same ID, the question, ten seconds, no cache-flush bit"
check "$(has ask ' direct-soon 192.168.166.1$')" "ask: the unicast-response bit, soon after the announcement: to the asker alone"
check "$(has ask ' direct-late 224.0.0.251$')" "ask: after 30 seconds without the record on the group: to the group"
check "$(has ask ' direct-again 192.168.166.1$')" "ask: and right after that to the asker alone again"
echo "logs: $work"
exit $status
