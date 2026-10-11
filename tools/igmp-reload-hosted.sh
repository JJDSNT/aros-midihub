#!/bin/sh
# Checks on Linux-hosted AROS that an interface removed by a reload of the
# stack's configuration and added again still answers IGMP queries.
#
#   tools/igmp-reload-hosted.sh [AROS_DIR [WORK_DIR]]
#
# As tools/igmp-hosted.sh, with a second interface: eth2 for AROS
# (192.168.167.2) and aroshost2 for Linux (192.168.167.1). AROS starts with
# both, reloads with eth2 taken out of db/interfaces, and reloads again with
# it back. A removed interface's ifnet is freed, and the new one is usually
# allocated where the old one was: a membership of the all-hosts group that
# still named the old one would make the new one look joined, the device
# would never be told the group's address, and queries would be filtered
# out before the stack. McastJoin then joins 239.77.1.2 on eth2 and the host
# sends three General Queries on aroshost2, each of which has to be answered
# with a report for that group.
# Exit status: 0 when both reloads were accepted and every query was answered.
set -eu

here=$(cd "$(dirname "$0")/.." && pwd)
aros=$(realpath "${1:-$here/build-aros-linux/bin/linux-x86_64/AROS}")
work=$(realpath "${2:-$(mktemp -d)}")
sys="$work/AROS"

test -x "$aros/boot/linux/AROSBootstrap"
test -f "$aros/Extras/aros-midihub/C/NetReload"
test -f "$aros/Extras/aros-midihub/C/McastJoin"

link() {
    if ! ip link show "$2" > /dev/null 2>&1; then
        sudo ip link add "$1" type veth peer name "$2"
        sudo ip addr add "$3" dev "$2"
    fi
    sudo ip link set "$1" up
    sudo ip link set "$2" up
    sudo sysctl -qw "net.ipv6.conf.$1.disable_ipv6=1"
    sudo ethtool -K "$2" tx off > /dev/null
    sudo ethtool -K "$1" tx off > /dev/null
}
link eth1 aroshost 192.168.166.1/24
link eth2 aroshost2 192.168.167.1/24

sudo rm -rf "$sys"
mkdir -p "$work"
cp -a --reflink=auto "$aros" "$sys"
db="$sys/System/Network/AROSTCP/db"
cp "$db/interfaces" "$db/interfaces.one"
echo 'eth1 DEV=DEVS:networks/eth.device UNIT=1 IP=192.168.166.2 NETMASK=255.255.255.0 UP' \
    >> "$db/interfaces.one"
cp "$db/interfaces.one" "$db/interfaces.two"
echo 'eth2 DEV=DEVS:networks/eth.device UNIT=2 IP=192.168.167.2 NETMASK=255.255.255.0 UP' \
    >> "$db/interfaces.two"
cp "$db/interfaces.two" "$db/interfaces"
printf 'HOST 192.168.166.2 arosbox.arosnet arosbox\nDOMAIN arosnet 192.168.166.\n' \
    > "$db/netdb-myhost"
sed -i 's/OPENGUI=.*/OPENGUI=NO/' "$db/general.config"
sudo setcap cap_net_raw,cap_net_admin+ep "$sys/boot/linux/AROSBootstrap"

c=SYS:Extras/aros-midihub/C
# IGMP_RELOAD_KEEP=1 reloads twice without taking eth2 out: the control.
first='Copy db/interfaces.one db/interfaces QUIET'
[ -z "${IGMP_RELOAD_KEEP:-}" ] || first='Copy db/interfaces.two db/interfaces QUIET'
cat > "$sys/S/igmp-run" <<RUNEOF
FailAt 21
CD SYS:System/Network/AROSTCP
Path C S ADD QUIET
Run >NIL: QUIET AROSTCP
WaitForPort AROSTCP
Wait 10
$first
$c/NetReload >SYS:igmp-reload-1.log
Wait 4
Copy db/interfaces.two db/interfaces QUIET
$c/NetReload >SYS:igmp-reload-2.log
Wait 3
Run >SYS:igmp-join.log $c/McastJoin 239.77.1.2 192.168.167.2 40
Echo "reloaded" >SYS:igmp-reloaded.log
Wait 30
ifconfig eth2 >SYS:igmp-ifconfig.log
netstat -s >SYS:igmp-netstat.log
Wait 5
Shutdown
RUNEOF
printf 'Run >NIL: Execute S:igmp-run\n' > "$sys/S/User-Startup"

sudo timeout 90 tcpdump -l -n -tt -i aroshost2 igmp > "$work/igmp.log" 2> /dev/null &

# Once AROS says it has reloaded twice and joined: five seconds, then three
# queries five seconds apart.
(
    waited=0
    while [ ! -f "$sys/igmp-reloaded.log" ] && [ $waited -lt 60 ]; do
        sleep 1
        waited=$((waited + 1))
    done
    sleep 5
    sudo python3 - "$work/queries.log" <<'PYEOF' || true
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
               socket.inet_aton('192.168.167.1'))
raw.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 1)
raw.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_LOOP, 0)
with open(sys.argv[1], 'w') as log:
    for _ in range(3):
        log.write('%f\n' % time.time())
        log.flush()
        raw.sendto(body, ('224.0.0.1', 0))
        time.sleep(5)
PYEOF
) &

(cd "$sys" && timeout 120 xvfb-run -a boot/linux/AROSBootstrap > "$work/serial.log" 2>&1) || true
wait

status=0
for n in 1 2; do
    if grep -q '^reload: 0' "$sys/igmp-reload-$n.log" 2> /dev/null; then
        echo "ok: reload $n was accepted"
    else
        echo "FAIL: reload $n: $(cat "$sys/igmp-reload-$n.log" 2> /dev/null || echo 'no answer')"
        status=1
    fi
done
answered=$(python3 - "$work/queries.log" "$work/igmp.log" <<'PYEOF'
import os, re, sys
queries = [float(l) for l in open(sys.argv[1]) if l.strip()] if os.path.exists(sys.argv[1]) else []
reports = [float(m.group(1)) for m in
           (re.match(r'(\d+\.\d+) IP 192\.168\.167\.2 > 239\.77\.1\.2: igmp .*report', l)
            for l in open(sys.argv[2])) if m]
print(sum(1 for q in queries if any(q < r < q + 3 for r in reports)), len(queries))
PYEOF
)
set -- $answered
if [ "${2:-0}" -gt 0 ] && [ "$1" = "$2" ]; then
    echo "ok: the interface that was removed and added again answered each of $2 IGMP General Queries"
else
    echo "FAIL: the interface that was removed and added again answered ${1:-0} of ${2:-0} IGMP General Queries (see $work/igmp.log)"
    status=1
fi
# for whoever looks into a failure
cp "$sys/igmp-ifconfig.log" "$sys/igmp-netstat.log" "$work/" 2> /dev/null || true
echo "logs: $work"
exit $status
