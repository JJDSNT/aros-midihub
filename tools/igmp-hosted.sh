#!/bin/sh
# Checks on Linux-hosted AROS that an IPv4 group membership survives an IGMP
# querier.
#
#   tools/igmp-hosted.sh [AROS_DIR [WORK_DIR]]
#
# AROS_DIR is a hosted system tree as tools/bonami-hosted.sh wants it; it is
# copied to WORK_DIR and not modified. The same veth pair is used: eth1 for
# AROS (192.168.166.2) and aroshost for Linux (192.168.166.1). Uses sudo for
# the pair, for tcpdump and for the raw socket that sends the queries.
#
# mdnsd, started by the AROSTCP service manager, joins 224.0.0.251. The host
# then plays the querier: three IGMPv2 General Queries to 224.0.0.1, which a
# member has to answer with a report for each of its groups. A stack that is
# not in the all-hosts group never sees a query, and a switch or router would
# stop forwarding the group after the membership interval.
# Exit status: 0 when AROS announced the group and answered every query.
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

cat > "$sys/S/igmp-run" <<EOF
FailAt 21
CD SYS:System/Network/AROSTCP
Path C S ADD QUIET
Run >NIL: QUIET AROSTCP
WaitForPort AROSTCP
Wait 40
Shutdown
EOF
printf 'Run >NIL: Execute S:igmp-run\n' > "$sys/S/User-Startup"

sudo timeout 60 tcpdump -l -n -tt -i aroshost igmp > "$work/igmp.log" 2> /dev/null &

# Queries at 20, 25 and 30 seconds, with one second to answer.
(sleep 20; sudo python3 - "$work/queries.log" <<'PYEOF' || true
import socket, struct, sys, time
def checksum(data):
    total = sum(struct.unpack('>%dH' % (len(data) // 2), data))
    while total >> 16:
        total = (total & 0xffff) + (total >> 16)
    return ~total & 0xffff
body = struct.pack('>BBH4s', 0x11, 10, 0, socket.inet_aton('0.0.0.0'))
body = body[:2] + struct.pack('>H', checksum(body)) + body[4:]
raw = socket.socket(socket.AF_INET, socket.SOCK_RAW, socket.IPPROTO_IGMP)
raw.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_IF,
               socket.inet_aton('192.168.166.1'))
raw.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 1)
# Linux is in 224.0.0.251 too (Avahi): it must not hear its own query and
# answer first, which would rightly silence AROS's report.
raw.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_LOOP, 0)
with open(sys.argv[1], 'w') as log:
    for _ in range(3):
        log.write('%f\n' % time.time())
        log.flush()
        raw.sendto(body, ('224.0.0.1', 0))
        time.sleep(5)
PYEOF
) &

(cd "$sys" && timeout 90 xvfb-run -a boot/linux/AROSBootstrap > "$work/serial.log" 2>&1) || true
wait

status=0
answered=$(python3 - "$work/queries.log" "$work/igmp.log" <<'PYEOF'
import re, sys
queries = [float(l) for l in open(sys.argv[1]) if l.strip()]
reports = [float(m.group(1)) for m in
           (re.match(r'(\d+\.\d+) IP 192\.168\.166\.2 > 224\.0\.0\.251: igmp .*report', l)
            for l in open(sys.argv[2])) if m]
print(sum(1 for q in queries if any(q < r < q + 3 for r in reports)), len(queries),
      sum(1 for r in reports if not queries or r < queries[0]))
PYEOF
)
set -- $answered
if [ "${2:-0}" -gt 0 ] && [ "$1" = "$2" ]; then
    echo "ok: AROS answered each of $2 IGMP General Queries with a report for 224.0.0.251"
else
    echo "FAIL: AROS answered ${1:-0} of ${2:-0} IGMP General Queries (see $work/igmp.log)"
    status=1
fi
if [ "${3:-0}" -gt 0 ]; then
    echo "ok: AROS announced the membership when it joined ($3 unsolicited reports)"
else
    echo "FAIL: no unsolicited report when AROS joined 224.0.0.251"
    status=1
fi
echo "logs: $work"
exit $status
