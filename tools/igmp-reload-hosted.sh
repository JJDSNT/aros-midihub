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
#
# mdnsd is told when a reload begins and ends, closes its sockets and opens
# them again: it has to be back in 224.0.0.251 on both interfaces, the one
# that stayed (the same queries go out on aroshost) and the one that came
# back, and report it.
#
# McastJoin then ends and closes its socket. A fourth query must find the
# group gone: a socket that closes takes its memberships with it.
# Exit status: 0 when both reloads were accepted, every query was answered
# for every group that was joined, and none for the group that was left.
set -eu

here=$(cd "$(dirname "$0")/.." && pwd)
aros=$(realpath "${1:-$here/build-aros-linux/bin/linux-x86_64/AROS}")
work=$(realpath "${2:-$(mktemp -d)}")
sys="$work/AROS"

test -x "$aros/boot/linux/AROSBootstrap"
test -f "$aros/System/Network/Services/mdnsd"
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
Run >SYS:igmp-join.log $c/McastJoin 239.77.1.2 192.168.167.2 22
Echo "reloaded" >SYS:igmp-reloaded.log
Wait 38
ifconfig eth2 >SYS:igmp-ifconfig.log
netstat -s >SYS:igmp-netstat.log
Wait 5
Shutdown
RUNEOF
printf 'Run >NIL: Execute S:igmp-run\n' > "$sys/S/User-Startup"

sudo timeout 110 tcpdump -l -n -tt -i aroshost2 igmp > "$work/igmp.log" 2> /dev/null &
sudo timeout 110 tcpdump -l -n -tt -i aroshost igmp > "$work/igmp-kept.log" 2> /dev/null &

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
raws = []
for address in ('192.168.167.1', '192.168.166.1'):
    raw = socket.socket(socket.AF_INET, socket.SOCK_RAW, socket.IPPROTO_IGMP)
    raw.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_IF,
                   socket.inet_aton(address))
    raw.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 1)
    # Linux is in 224.0.0.251 too (Avahi) and must not answer first
    raw.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_LOOP, 0)
    raws.append(raw)
with open(sys.argv[1], 'w') as log:
    for _ in range(3):
        log.write('%f\n' % time.time())
        log.flush()
        for raw in raws:
            raw.sendto(body, ('224.0.0.1', 0))
        time.sleep(5)
# McastJoin is a member for 22 seconds from before the first query: by now
# it has ended.
time.sleep(12)
with open(sys.argv[1] + '.late', 'w') as log:
    log.write('%f\n' % time.time())
    log.flush()
    raws[0].sendto(body, ('224.0.0.1', 0))
    time.sleep(3)
PYEOF
) &

(cd "$sys" && timeout 140 xvfb-run -a boot/linux/AROSBootstrap > "$work/serial.log" 2>&1) || true
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
count_answers() {
    python3 - "${3:-$work/queries.log}" "$1" "$2" <<'PYEOF'
import os, re, sys
queries = [float(l) for l in open(sys.argv[1]) if l.strip()] if os.path.exists(sys.argv[1]) else []
reports = [float(m.group(1)) for m in
           (re.match(r'(\d+\.\d+) IP ' + sys.argv[3] + r': igmp .*report', l)
            for l in open(sys.argv[2])) if m]
print(sum(1 for q in queries if any(q < r < q + 3 for r in reports)), len(queries))
PYEOF
}
answered=$(count_answers "$work/igmp.log" '192\.168\.167\.2 > 239\.77\.1\.2')
set -- $answered
if [ "${2:-0}" -gt 0 ] && [ "$1" = "$2" ]; then
    echo "ok: the interface that was removed and added again answered each of $2 IGMP General Queries"
else
    echo "FAIL: the interface that was removed and added again answered ${1:-0} of ${2:-0} IGMP General Queries (see $work/igmp.log)"
    status=1
fi
answered=$(count_answers "$work/igmp-kept.log" '192\.168\.166\.2 > 224\.0\.0\.251')
set -- $answered
if [ "${2:-0}" -gt 0 ] && [ "$1" = "$2" ]; then
    echo "ok: mdnsd is in 224.0.0.251 after the reloads on the interface that stayed ($2 queries)"
else
    echo "FAIL: after the reloads mdnsd's group was reported for ${1:-0} of ${2:-0} queries on the interface that stayed (see $work/igmp-kept.log)"
    status=1
fi
answered=$(count_answers "$work/igmp.log" '192\.168\.167\.2 > 224\.0\.0\.251')
set -- $answered
if [ "${2:-0}" -gt 0 ] && [ "$1" = "$2" ]; then
    echo "ok: and on the interface that was removed and added again ($2 queries)"
else
    echo "FAIL: after the reloads mdnsd's group was reported for ${1:-0} of ${2:-0} queries on the interface that came back (see $work/igmp.log)"
    status=1
fi
# mdnsd closed its sockets at the first reload and opened new ones: a
# membership that survived the close would answer without ever joining again
joins=$(python3 - "$work/queries.log" "$work/igmp-kept.log" <<'PYEOF'
import os, re, sys
queries = [float(l) for l in open(sys.argv[1]) if l.strip()] if os.path.exists(sys.argv[1]) else [0]
times = [float(m.group(1)) for m in
         (re.match(r'(\d+\.\d+) IP 192\.168\.166\.2 > 224\.0\.0\.251: igmp .*report', l)
          for l in open(sys.argv[2])) if m]
# joins before the first query, counted as bursts at least two seconds apart
before = [t for t in times if t < queries[0]]
print(sum(1 for i, t in enumerate(before) if i == 0 or t - before[i - 1] > 2))
PYEOF
)
if [ "${joins:-0}" -ge 2 ]; then
    echo "ok: mdnsd announced the group again after a reload ($joins times in all)"
else
    echo "FAIL: mdnsd announced its group ${joins:-0} time(s): it never joined again after a reload"
    status=1
fi
late=$(count_answers "$work/igmp.log" '192\.168\.167\.2 > 239\.77\.1\.2' "$work/queries.log.late")
set -- $late
if [ "${2:-0}" -gt 0 ] && [ "$1" = "0" ]; then
    echo "ok: the group McastJoin was in is no longer reported after its socket closed"
else
    echo "FAIL: 239.77.1.2 was still reported after the socket that joined it closed (${1:-?} of ${2:-0} queries)"
    status=1
fi
# for whoever looks into a failure
cp "$sys/igmp-ifconfig.log" "$sys/igmp-netstat.log" "$work/" 2> /dev/null || true
echo "logs: $work"
exit $status
