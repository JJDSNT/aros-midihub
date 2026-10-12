#!/bin/sh
# Checks on Linux-hosted AROS how mdnsd treats the host's own name.
#
#   tools/mdns-hostname-hosted.sh [AROS_DIR [WORK_DIR]]
#
# As tools/bonami-hosted.sh: eth1 for AROS (192.168.166.2, host name
# arosbox), aroshost for Linux (192.168.166.1). Four boots.
#
# 1. Nobody else has the name. AROS has to probe for arosbox.local three
#    times, announce its address, and answer a question for the name while
#    it offers no service at all.
# 2. Another host has the name: the Linux side answers AROS's first probe
#    with an address record of its own. AROS has to take arosbox-2.local,
#    probe for that, announce it and answer for it, and a service it
#    registers has to point at the new name.
# 3. Another host turns up with the name later: once AROS has announced the
#    name and a service under it, the Linux side sends an address record
#    for arosbox.local. AROS has to give the name up, take arosbox-2.local
#    and announce the service again pointing at it.
# 4. Another host probes for the name at the same moment, with an address
#    that sorts after AROS's. AROS has to give way for a second and probe
#    again (RFC 6762 section 8.2); the other host never announces, so AROS
#    gets arosbox.local in the end.
# Exit status: 0 when all four went that way.
set -eu

here=$(cd "$(dirname "$0")/.." && pwd)
aros=$(realpath "${1:-$here/build-aros-linux/bin/linux-x86_64/AROS}")
work=$(realpath "${2:-$(mktemp -d)}")
sys="$work/AROS"
type=_bonamihost._udp

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

c=SYS:Extras/aros-midihub/C

# boot NAME MODE: what the Linux side does about arosbox.local. 0 nothing,
# 1 answers the first probe, 2 claims it after AROS announced a service,
# 3 probes for it at the same time as AROS.
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
    cat > "$sys/S/host-run" <<EOF
FailAt 21
CD SYS:System/Network/AROSTCP
Path C S ADD QUIET
Run >NIL: QUIET AROSTCP
WaitForPort AROSTCP
Wait 14
Run >NIL: $c/BonamiProbe REGISTER HostSvc TYPE $type PORT 5004 SECONDS 14
Wait 16
Shutdown
EOF
    printf 'Run >NIL: Execute S:host-run\n' > "$sys/S/User-Startup"

    sudo timeout 60 python3 - "$work/host-$1.log" "$2" "$type" <<'PYEOF' &
import socket, struct, sys, time

GROUP, PORT, HOST = '224.0.0.251', 5353, '192.168.166.1'
log = open(sys.argv[1], 'w')
mode = int(sys.argv[2])
conflict = mode == 1
kind = sys.argv[3]

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
rx.settimeout(0.2)
tx = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
tx.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_IF, socket.inet_aton(HOST))
tx.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 255)
tx.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_LOOP, 0)

claimed = False
announced_service = False
claim_at = None
end = time.time() + 50
while time.time() < end:
    if claim_at is not None and not claimed and time.time() >= claim_at:
        claimed = True
        # some other host has had the name all along and says so now
        packet = struct.pack('>HHHHHH', 0, 0x8400, 0, 1, 0, 0) + wire('arosbox.local') + \
            struct.pack('>HHIH', 1, 0x8001, 120, 4) + socket.inet_aton('192.168.166.77')
        tx.sendto(packet, (GROUP, PORT))
        log.write('%f claimed arosbox.local\n' % time.time())
        log.flush()
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
            questions.append(name.lower())
            offset += 4
        records = []
        for _ in range(an + ns + ar):
            name, offset = read_name(data, offset)
            rtype, rclass, ttl, rdlen = struct.unpack('>HHIH', data[offset:offset + 10])
            rdata = data[offset + 10:offset + 10 + rdlen]
            target = ''
            if rtype == 33:
                target, _ = read_name(data, offset + 16)
            offset += 10 + rdlen
            records.append((name.lower(), rtype, ttl, rdata, target.lower()))
    except (IndexError, struct.error):
        continue
    now = time.time()
    response = bool(flags & 0x8000)
    for host in ('arosbox.local', 'arosbox-2.local'):
        if not response and ns and host in questions:
            log.write('%f probe %s\n' % (now, host))
            if conflict and host == 'arosbox.local' and not claimed:
                claimed = True
                # the host that already has the name answers the probe
                packet = struct.pack('>HHHHHH', 0, 0x8400, 0, 1, 0, 0) + wire(host) + \
                    struct.pack('>HHIH', 1, 0x8001, 120, 4) + socket.inet_aton('192.168.166.77')
                tx.sendto(packet, (GROUP, PORT))
                log.write('%f claimed %s\n' % (time.time(), host))
            if mode == 3 and host == 'arosbox.local' and not claimed:
                claimed = True
                # a probe of our own for the same name, as a second host;
                # 192.168.166.200 sorts after 192.168.166.2
                packet = struct.pack('>HHHHHH', 0, 0, 1, 0, 1, 0) + wire(host) + \
                    struct.pack('>HH', 255, 1) + wire(host) + \
                    struct.pack('>HHIH', 1, 1, 120, 4) + socket.inet_aton('192.168.166.200')
                tx.sendto(packet, (GROUP, PORT))
                log.write('%f competed %s\n' % (time.time(), host))
        if response:
            for name, rtype, ttl, rdata, target in records:
                if name == host and rtype == 1 and ttl and rdata == socket.inet_aton('192.168.166.2'):
                    log.write('%f address %s\n' % (now, host))
    if response:
        for name, rtype, ttl, rdata, target in records:
            if rtype == 33 and ttl and name.startswith('hostsvc.'):
                log.write('%f target %s\n' % (now, target))
                announced_service = True
    if mode == 2 and announced_service and claim_at is None:
        claim_at = now + 2.5    # after the second announcements
    log.flush()
PYEOF

    # Ten seconds in, with no service registered yet: who is arosbox?
    (sleep 20; for name in arosbox.local arosbox-2.local; do
        out=$(timeout 4 avahi-resolve -4 -n "$name" 2> /dev/null || true)
        echo "$name ${out:--}"
    done > "$work/resolve-$1.log") &

    (cd "$sys" && timeout 70 xvfb-run -a boot/linux/AROSBootstrap > "$work/serial-$1.log" 2>&1) || true
    wait
}

count() { grep -c " $2 $3\$" "$work/host-$1.log" || true; }
resolved() { grep "^$2 " "$work/resolve-$1.log" | grep -c '192\.168\.166\.2' || true; }

status=0
check() {
    if [ "$1" = yes ]; then
        echo "ok: $2"
    else
        echo "FAIL: $2"
        status=1
    fi
}
yes_if() { if "$@"; then echo yes; else echo no; fi; }

boot plain 0
p=$(count plain probe arosbox.local)
check "$(yes_if [ "$p" -eq 3 ])" "nobody else has the name: three probes for arosbox.local ($p)"
check "$(yes_if [ "$(count plain address arosbox.local)" -ge 2 ])" "its address is announced, twice"
check "$(yes_if [ "$(count plain probe arosbox-2.local)" -eq 0 ])" "and no other name is tried"
check "$(yes_if [ "$(resolved plain arosbox.local)" -eq 1 ])" "Avahi resolves arosbox.local while AROS offers no service"
check "$(yes_if grep -q ' target arosbox.local$' "$work/host-plain.log")" "a service registered later points at arosbox.local"

boot taken 1
check "$(yes_if [ "$(count taken claimed arosbox.local)" -eq 1 ])" "another host answers the first probe for arosbox.local"
p=$(count taken probe arosbox-2.local)
check "$(yes_if [ "$p" -eq 3 ])" "AROS probes for arosbox-2.local instead ($p probes)"
check "$(yes_if [ "$(count taken address arosbox-2.local)" -ge 2 ])" "and announces its address under that name"
check "$(yes_if [ "$(count taken address arosbox.local)" -eq 0 ])" "never under the name the other host has"
check "$(yes_if [ "$(resolved taken arosbox-2.local)" -eq 1 ])" "Avahi resolves arosbox-2.local to AROS"
check "$(yes_if grep -q ' target arosbox-2.local$' "$work/host-taken.log")" "a service registered later points at arosbox-2.local"
if grep -q ' target arosbox.local$' "$work/host-taken.log"; then
    check no "no service record points at the name the other host has"
fi
boot late 2
check "$(yes_if [ "$(count late address arosbox.local)" -ge 2 ])" "AROS announces arosbox.local, and a service under it"
check "$(yes_if [ "$(count late claimed arosbox.local)" -eq 1 ])" "then another host says the name is its own"
p=$(count late probe arosbox-2.local)
check "$(yes_if [ "$p" -eq 3 ])" "AROS gives the name up and probes for arosbox-2.local ($p probes)"
check "$(yes_if [ "$(count late address arosbox-2.local)" -ge 2 ])" "and announces its address under that name"
after=$(python3 - "$work/host-late.log" <<'PYEOF'
import sys
claimed, old, new = None, 0, 0
for line in open(sys.argv[1]):
    when, what, name = line.split()
    if what == 'claimed':
        claimed = float(when)
    elif what == 'target' and claimed is not None and float(when) > claimed:
        if name == 'arosbox-2.local':
            new += 1
        else:
            old += 1
print(new, old)
PYEOF
)
set -- $after
check "$(yes_if [ "${1:-0}" -ge 1 ])" "the service is announced again pointing at arosbox-2.local ($1 times)"
check "$(yes_if [ "${2:-1}" -eq 0 ])" "and no longer at the name it lost"

boot tie 3
check "$(yes_if [ "$(count tie competed arosbox.local)" -eq 1 ])" "another host probes for arosbox.local at the same time, with a later address"
gap=$(python3 - "$work/host-tie.log" <<'PYEOF'
import sys
competed, probes, announced = None, [], None
for line in open(sys.argv[1]):
    when, what, name = line.split()
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
check "$(yes_if awk "BEGIN{exit !(${1:-0} >= 4 && ${2:-0} >= 0.9)}")" "AROS gives way: $1 probes in all, the next one $2 s after the other host's"
check "$(yes_if [ "${3:-0}" -eq 1 ])" "and has arosbox.local in the end, as the other host never announced"
check "$(yes_if [ "$(count tie probe arosbox-2.local)" -eq 0 ])" "without trying another name"
echo "logs: $work"
exit $status
