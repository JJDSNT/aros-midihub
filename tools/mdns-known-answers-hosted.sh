#!/bin/sh
# Checks on Linux-hosted AROS that mdnsd's browse questions list the answers
# it already has (RFC 6762 section 7.1).
#
#   tools/mdns-known-answers-hosted.sh [AROS_DIR [WORK_DIR]]
#
# As tools/bonami-hosted.sh: eth1 for AROS (192.168.166.2), aroshost for
# Linux (192.168.166.1). Avahi publishes KnownSvc. AROS registers a service
# of the same type and browses the type for a quarter of a minute, asking
# several times at growing intervals. The host reads every question AROS
# asks for the type and the PTR records in its answer section.
# mdnsd keeps what it hears whether or not anybody browses, so it may know
# KnownSvc before the first question.
# Exit status: 0 when the questions listed KnownSvc with more than half its
# life to go, none listed AROS's own service, and the browse still found
# KnownSvc.
set -eu

here=$(cd "$(dirname "$0")/.." && pwd)
aros=$(realpath "${1:-$here/build-aros-linux/bin/linux-x86_64/AROS}")
work=$(realpath "${2:-$(mktemp -d)}")
sys="$work/AROS"
type=_bonamiknown._udp

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
cat > "$sys/S/known-run" <<RUNEOF
FailAt 21
CD SYS:System/Network/AROSTCP
Path C S ADD QUIET
Run >NIL: QUIET AROSTCP
WaitForPort AROSTCP
Wait 6
$c/BonamiProbe REGISTER ArosOwn TYPE $type PORT 5004 BROWSE $type SECONDS 16 >SYS:known-probe.log
Wait 2
Shutdown
RUNEOF
printf 'Run >NIL: Execute S:known-run\n' > "$sys/S/User-Startup"

(timeout 60 avahi-publish -s KnownSvc "$type" 6000 > "$work/publish.log" 2>&1 || true) &

sudo timeout 55 python3 - "$work/known.log" "$type" <<'PYEOF' &
import socket, struct, sys, time

GROUP, PORT, HOST = '224.0.0.251', 5353, '192.168.166.1'
log = open(sys.argv[1], 'w')
wanted = (sys.argv[2] + '.local').lower()

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
end = time.time() + 48
while time.time() < end:
    try:
        data, source = rx.recvfrom(9000)
    except socket.timeout:
        continue
    if source[0] != '192.168.166.2' or len(data) < 12:
        continue
    ident, flags, qd, an, ns, ar = struct.unpack('>HHHHHH', data[:12])
    if flags & 0x8000:
        continue
    offset = 12
    try:
        asked = False
        for _ in range(qd):
            name, offset = read_name(data, offset)
            qtype, qclass = struct.unpack('>HH', data[offset:offset + 4])
            offset += 4
            asked = asked or (name.lower() == wanted and qtype == 12)
        known = []
        for _ in range(an):
            name, offset = read_name(data, offset)
            rtype, rclass, ttl, rdlen = struct.unpack('>HHIH', data[offset:offset + 10])
            if rtype == 12:
                target, _ = read_name(data, offset + 10)
                known.append('%s/%d' % (target.split('.')[0], ttl))
            offset += 10 + rdlen
    except (IndexError, struct.error):
        continue
    if asked:
        log.write('%f %d %d %s\n' % (time.time(), len(data), len(known), ','.join(known) or '-'))
        log.flush()
PYEOF

(cd "$sys" && timeout 65 xvfb-run -a boot/linux/AROSBootstrap > "$work/serial.log" 2>&1) || true
wait

status=0
check() {
    if [ "$1" = yes ]; then echo "ok: $2"; else echo "FAIL: $2"; status=1; fi
}
questions=$(wc -l < "$work/known.log")
young=$(tr ',' '\n' < "$work/known.log" | sed -n 's/.*KnownSvc\/\([0-9]*\).*/\1/p' | awk '$1 <= 2250' | wc -l)
listed=$(grep -c 'KnownSvc/' "$work/known.log" || true)
own=$(grep -c 'ArosOwn' "$work/known.log" || true)
check "$([ "$questions" -ge 3 ] && echo yes || echo no)" "AROS asked for the type $questions times"
check "$([ "$listed" -ge 1 ] && echo yes || echo no)" "$listed of them listed KnownSvc as known"
check "$([ "$young" -eq 0 ] && echo yes || echo no)" "always with more than half of its 4500 seconds to go"
check "$([ "$own" -eq 0 ] && echo yes || echo no)" "none listed AROS's own service"
check "$(grep -q 'KnownSvc' "$sys/known-probe.log" 2> /dev/null && echo yes || echo no)" "and the browse still found KnownSvc"
echo "logs: $work"
exit $status
