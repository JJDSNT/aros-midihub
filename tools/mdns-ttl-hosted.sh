#!/bin/sh
# Checks on Linux-hosted AROS that a service registered with a time to live
# is announced with it, and one registered without is announced with the
# defaults (RFC 6762 section 10: 75 minutes, and 2 for the record that
# names the host). Network MIDI 2.0 wants its service's records to live a
# minute or less (M2-124-UM section 4.6).
#
#   tools/mdns-ttl-hosted.sh [AROS_DIR [WORK_DIR]]
#
# eth1 is AROS's (192.168.166.2), aroshost the host's (192.168.166.1). It
# needs passwordless sudo, tcpdump, python3, ethtool and xvfb-run.
#
# AROS registers "Short Lived" with 60 seconds and "Long Lived" with
# nothing said; the host reads the announcements off the wire. A third
# registration, with 0 seconds, has to be refused.
# Exit status: 0 when all three went as they should.
set -eu

here=$(cd "$(dirname "$0")/.." && pwd)
aros=$(realpath "${1:-$here/build-aros-linux/bin/linux-x86_64/AROS}")
work=$(realpath "${2:-$(mktemp -d)}")
sys="$work/AROS"
c=SYS:Extras/aros-midihub/C
type=_ttltest._udp

test -x "$aros/boot/linux/AROSBootstrap"
test -f "$aros/Extras/aros-midihub/C/dnssd"
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
cat > "$sys/S/ttl-run" <<RUNEOF
FailAt 21
CD SYS:System/Network/AROSTCP
Path C S ADD QUIET
Run >NIL: QUIET AROSTCP
WaitForPort AROSTCP
Wait 8
Run >SYS:ttl-short.log $c/dnssd -R "Short Lived" $type . 5000 -t 60 "k=v"
Run >SYS:ttl-long.log $c/dnssd -R "Long Lived" $type . 5001 "k=v"
$c/dnssd >SYS:ttl-zero.log -R "Not Lived" $type . 5002 -t 0 "k=v"
$c/dnssd >SYS:ttl-over.log -R "Over Lived" $type . 5003 -t 4501 "k=v"
Wait 12
Break \`Status COMMAND=$c/dnssd\` C
Wait 1
Break \`Status COMMAND=$c/dnssd\` C
Wait 3
Shutdown
RUNEOF
printf 'Run >NIL: Execute S:ttl-run\n' > "$sys/S/User-Startup"

(sudo timeout 60 tcpdump -i aroshost -n -U -w "$work/mdns.pcap" udp port 5353 \
    > /dev/null 2>&1 || true) &
(cd "$sys" && timeout 70 xvfb-run -a boot/linux/AROSBootstrap \
    > "$work/serial.log" 2>&1) || true
wait || true
cp "$sys"/ttl-*.log "$work/" 2> /dev/null || true

status=0
sudo chmod a+r "$work/mdns.pcap" 2> /dev/null || true
# Every record AROS sent in an answer, one to a line: name, type, seconds.
# Goodbyes, which have 0 seconds, are left out.
sent=$(python3 -I - "$work/mdns.pcap" <<'PYEOF'
import struct, sys
data = open(sys.argv[1], 'rb').read()
def name(m, o, depth=0):
    parts = []
    while depth < 8:
        n = m[o]
        if n == 0:
            return '.'.join(parts), o + 1
        if n & 0xc0:
            rest, _ = name(m, ((n & 0x3f) << 8) | m[o + 1], depth + 1)
            return '.'.join(parts + [rest]), o + 2
        parts.append(m[o + 1:o + 1 + n].decode('utf-8', 'replace'))
        o += 1 + n
    return '?', o
types = {1: 'A', 12: 'PTR', 16: 'TXT', 33: 'SRV'}
o = 24
while o + 16 <= len(data):
    caplen = struct.unpack('<I', data[o + 8:o + 12])[0]
    frame = data[o + 16:o + 16 + caplen]
    o += 16 + caplen
    if len(frame) < 42 or frame[12:14] != b'\x08\x00':
        continue
    ihl = (frame[14] & 15) * 4
    if frame[26:30] != bytes([192, 168, 166, 2]):
        continue
    m = frame[14 + ihl + 8:]
    try:
        qd, an = struct.unpack('>HH', m[4:8])
        p = 12
        for _ in range(qd):
            _, p = name(m, p)
            p += 4
        for _ in range(an):
            n, p = name(m, p)
            t, _, ttl, rdlen = struct.unpack('>HHIH', m[p:p + 10])
            p += 10 + rdlen
            if ttl:
                print(n, types.get(t, t), ttl)
    except (IndexError, struct.error):
        pass
PYEOF
)
has() { echo "$sent" | grep -q "^$1\$"; }
if has 'Short Lived._ttltest._udp.local SRV 60' &&
   has 'Short Lived._ttltest._udp.local TXT 60' &&
   has '_ttltest._udp.local PTR 60' &&
   has 'arosbox.local A 120'; then
    echo "ok: the service registered with 60 seconds is announced with them, the host's address with its own 120"
else
    echo "FAIL: the 60 seconds are not on the wire"
    status=1
fi
if has 'Long Lived._ttltest._udp.local SRV 120' &&
   has 'Long Lived._ttltest._udp.local TXT 4500' &&
   has '_ttltest._udp.local PTR 4500'; then
    echo "ok: the one registered without is announced with the defaults"
else
    echo "FAIL: the defaults are not on the wire"
    status=1
fi
if echo "$sent" | grep -q 'Not Lived\|Over Lived'; then
    echo "FAIL: a registration with 0 or 4501 seconds was announced"
    status=1
else
    echo "ok: 0 and 4501 seconds are refused: $(head -1 "$work/ttl-zero.log" 2> /dev/null)"
fi
echo "logs: $work"
exit $status
