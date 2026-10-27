#!/bin/sh
# Checks on Linux-hosted AROS that mdnsd delays its answers to a browse
# question and sends them together (RFC 6762 section 6).
#
#   tools/mdns-answer-delay-hosted.sh [AROS_DIR [WORK_DIR [MDNSD]]]
#
# As tools/bonami-hosted.sh: eth1 for AROS (192.168.166.2), aroshost for
# Linux (192.168.166.1). AROS registers three services of one type. The host
# asks for the type five times, two seconds apart, then repeats browse and
# service-type enumeration questions inside and outside the one-second
# multicast suppression window. It reads what comes back within 600 ms: how
# many packets, how many PTR and address records, and how long after the
# question the first packet came.
# MDNSD may name a prospective daemon binary to install in the temporary
# image while retaining the known-good hosted system around it.
# Exit status: 0 when every question got one packet with the three PTR
# records and at least one address record, 20 to 200 ms after the question,
# and a question for one instance by name was answered within 15 ms. Repeated
# shared PTR questions must be silent inside one second and answer again
# after it; service-type enumeration follows the same delayed behavior.
set -eu

here=$(cd "$(dirname "$0")/.." && pwd)
aros=$(realpath "${1:-$here/build-aros-linux/bin/linux-x86_64/AROS}")
work=$(realpath "${2:-$(mktemp -d)}")
sys="$work/AROS"
type=_bonamidelay._udp
mdnsd=${3:-}

test -x "$aros/boot/linux/AROSBootstrap"
test -f "$aros/System/Network/Services/mdnsd"
test -f "$aros/Extras/aros-midihub/C/BonamiProbe"
if [ -n "$mdnsd" ]; then
    mdnsd=$(realpath "$mdnsd")
    test -f "$mdnsd"
fi

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
if [ -n "$mdnsd" ]; then
    cp "$mdnsd" "$sys/System/Network/Services/mdnsd"
fi
db="$sys/System/Network/AROSTCP/db"
echo 'eth1 DEV=DEVS:networks/eth.device UNIT=1 IP=192.168.166.2 NETMASK=255.255.255.0 UP' \
    >> "$db/interfaces"
printf 'HOST 192.168.166.2 arosbox.arosnet arosbox\nDOMAIN arosnet 192.168.166.\n' \
    > "$db/netdb-myhost"
sed -i 's/OPENGUI=.*/OPENGUI=NO/' "$db/general.config"
sudo setcap cap_net_raw,cap_net_admin+ep "$sys/boot/linux/AROSBootstrap"

c=SYS:Extras/aros-midihub/C
cat > "$sys/S/delay-run" <<RUNEOF
FailAt 21
CD SYS:System/Network/AROSTCP
Path C S ADD QUIET
Run >NIL: QUIET AROSTCP
WaitForPort AROSTCP
Wait 6
Run >NIL: $c/BonamiProbe REGISTER DelayOne TYPE $type PORT 5001 SECONDS 30
Run >NIL: $c/BonamiProbe REGISTER DelayTwo TYPE $type PORT 5002 SECONDS 30
Run >NIL: $c/BonamiProbe REGISTER DelayThree TYPE $type PORT 5003 SECONDS 30
Echo "registered" >SYS:delay-registered.log
Wait 28
Shutdown
RUNEOF
printf 'Run >NIL: Execute S:delay-run\n' > "$sys/S/User-Startup"

(
    waited=0
    while [ ! -f "$sys/delay-registered.log" ] && [ $waited -lt 60 ]; do
        sleep 1
        waited=$((waited + 1))
    done
    sleep 6
    sudo python3 - "$work/delay.log" "$type" <<'PYEOF' || true
import socket, struct, sys, time

GROUP, PORT, HOST = '224.0.0.251', 5353, '192.168.166.1'
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

# one socket on the mDNS port: a question from another port asks for a
# unicast answer, which is not what is measured here
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
s.bind(('', PORT))
s.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP,
             socket.inet_aton(GROUP) + socket.inet_aton(HOST))
s.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_IF, socket.inet_aton(HOST))
s.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 255)
s.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_LOOP, 0)

def ask(name, qtype, label, ptr_target=None):
    question = struct.pack('>HHHHHH', 0, 0, 1, 0, 0, 0) + wire(name) + struct.pack('>HH', qtype, 1)
    s.sendto(question, (GROUP, PORT))
    sent = time.time()
    packets, ptrs, addrs, srvs, first = 0, 0, 0, 0, None
    while True:
        left = sent + 0.6 - time.time()
        if left <= 0:
            break
        s.settimeout(left)
        try:
            data, source = s.recvfrom(9000)
        except socket.timeout:
            break
        got = time.time()
        if source[0] != '192.168.166.2' or len(data) < 12:
            continue
        ident, flags, qd, an, ns, ar = struct.unpack('>HHHHHH', data[:12])
        if not flags & 0x8000:
            continue
        offset = 12
        try:
            for _ in range(qd):
                _, offset = read_name(data, offset)
                offset += 4
            mine = 0
            for _ in range(an + ns + ar):
                owner, offset = read_name(data, offset)
                rtype, rclass, ttl, rdlen = struct.unpack('>HHIH', data[offset:offset + 10])
                rdata = offset + 10
                offset = rdata + rdlen
                if rtype == 12 and owner.lower() == name.lower():
                    target, _ = read_name(data, rdata)
                    if ptr_target is None or target.lower() == ptr_target.lower():
                        ptrs += 1
                        mine += 1
                elif rtype == 33 and owner.lower().endswith(kind):
                    srvs += 1
                    mine += 1
                elif rtype == 1 and owner.lower() == 'arosbox.local':
                    addrs += 1
        except (IndexError, struct.error):
            continue
        if mine:
            packets += 1
            if first is None:
                first = got - sent
    log.write('%s %d %d %d %d %.4f\n' % (label, packets, ptrs, srvs, addrs,
                                          first if first is not None else -1))
    log.flush()

for _ in range(5):
    ask(kind, 12, 'browse')
    time.sleep(1.4)
ask('DelayTwo.' + kind, 33, 'named')
ask(kind, 12, 'repeat-first')
ask(kind, 12, 'repeat-blocked')
ask(kind, 12, 'repeat-after')
types = '_services._dns-sd._udp.local'
ask(types, 12, 'types-first', kind)
ask(types, 12, 'types-blocked', kind)
ask(types, 12, 'types-after', kind)
PYEOF
) &

(cd "$sys" && timeout 80 xvfb-run -a boot/linux/AROSBootstrap > "$work/serial.log" 2>&1) || true
wait

status=0
check() {
    if [ "$1" = yes ]; then echo "ok: $2"; else echo "FAIL: $2"; status=1; fi
}
browse=$(grep -c '^browse ' "$work/delay.log" || true)
one=$(awk '$1 == "browse" && $2 == 1 && $3 == 3 && $5 >= 1' "$work/delay.log" | wc -l)
timed=$(awk '$1 == "browse" && $6 >= 0.02 && $6 <= 0.2' "$work/delay.log" | wc -l)
delays=$(awk '$1 == "browse" {printf "%s%d", sep, $6 * 1000; sep=", "}' "$work/delay.log")
named=$(awk '$1 == "named" && $2 == 1 && $4 == 1 && $6 >= 0 && $6 < 0.015' "$work/delay.log" | wc -l)
namedms=$(awk '$1 == "named" {printf "%.1f", $6 * 1000}' "$work/delay.log")
repeat_first=$(awk '$1 == "repeat-first" && $2 == 1 && $3 == 3 && $5 >= 1 && $6 >= 0.02 && $6 <= 0.2' "$work/delay.log" | wc -l)
repeat_blocked=$(awk '$1 == "repeat-blocked" && $2 == 0' "$work/delay.log" | wc -l)
repeat_after=$(awk '$1 == "repeat-after" && $2 == 1 && $3 == 3 && $5 >= 1 && $6 >= 0.02 && $6 <= 0.2' "$work/delay.log" | wc -l)
types_first=$(awk '$1 == "types-first" && $2 == 1 && $3 == 1 && $5 == 0 && $6 >= 0.02 && $6 <= 0.2' "$work/delay.log" | wc -l)
types_blocked=$(awk '$1 == "types-blocked" && $2 == 0' "$work/delay.log" | wc -l)
types_after=$(awk '$1 == "types-after" && $2 == 1 && $3 == 1 && $5 == 0 && $6 >= 0.02 && $6 <= 0.2' "$work/delay.log" | wc -l)
check "$([ "$browse" -eq 5 ] && [ "$one" -eq 5 ] && echo yes || echo no)" "each of $browse browse questions got one packet with three PTR records and at least one address record ($one)"
check "$([ "$browse" -eq 5 ] && [ "$timed" -eq 5 ] && echo yes || echo no)" "after a delay of 20 to 200 ms: $delays ms"
check "$([ "$named" -eq 1 ] && echo yes || echo no)" "a question for one instance by name was answered at once, in one packet ($namedms ms)"
check "$([ "$repeat_first" -eq 1 ] && [ "$repeat_blocked" -eq 1 ] && [ "$repeat_after" -eq 1 ] && echo yes || echo no)" "ordinary shared PTR repeats are suppressed inside one second and resume afterward"
check "$([ "$types_first" -eq 1 ] && [ "$types_blocked" -eq 1 ] && [ "$types_after" -eq 1 ] && echo yes || echo no)" "service-type PTR answers are delayed and suppress repeats per RFC 6762"
echo "logs: $work"
exit $status
