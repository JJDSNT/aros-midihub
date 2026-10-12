#!/bin/sh
# Checks on Linux-hosted AROS how mdnsd treats the host's own name.
#
#   tools/mdns-hostname-hosted.sh [AROS_DIR [WORK_DIR]]
#
# As tools/bonami-hosted.sh: eth1 for AROS (192.168.166.2, host name
# arosbox), aroshost for Linux (192.168.166.1). Two boots.
#
# 1. Nobody else has the name. AROS has to probe for arosbox.local three
#    times, announce its address, and answer a question for the name while
#    it offers no service at all.
# 2. Another host has the name: the Linux side answers AROS's first probe
#    with an address record of its own. AROS has to take arosbox-2.local,
#    probe for that, announce it and answer for it, and a service it
#    registers has to point at the new name.
# Exit status: 0 when both went that way.
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

# boot NAME CONFLICT: CONFLICT=1 makes the Linux side claim arosbox.local.
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
conflict = sys.argv[2] == '1'
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
rx.settimeout(1)
tx = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
tx.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_IF, socket.inet_aton(HOST))
tx.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 255)
tx.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_LOOP, 0)

claimed = False
end = time.time() + 50
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
        if response:
            for name, rtype, ttl, rdata, target in records:
                if name == host and rtype == 1 and ttl and rdata == socket.inet_aton('192.168.166.2'):
                    log.write('%f address %s\n' % (now, host))
    if response:
        for name, rtype, ttl, rdata, target in records:
            if rtype == 33 and ttl and name.startswith('hostsvc.'):
                log.write('%f target %s\n' % (now, target))
    log.flush()
PYEOF

    # Ten seconds in, with no service registered yet: who is arosbox?
    (sleep 20; for name in arosbox.local arosbox-2.local; do
        printf '%s ' "$name"
        timeout 4 avahi-resolve -4 -n "$name" 2> /dev/null || echo "-"
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
echo "logs: $work"
exit $status
