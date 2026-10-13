#!/bin/sh
# Checks on Linux-hosted AROS what a client gets from the registering half
# of bonami.library: a service's four records as it gave them, a second
# registration of the same name refused, a changed port and text, a
# service offered for another host, the goodbyes when the clients withdraw,
# and nothing left of a client that ended without closing the library.
#
#   tools/mdns-register-hosted.sh [AROS_DIR [WORK_DIR]]
#
# eth1 is AROS's (192.168.166.2), aroshost the host's (192.168.166.1). It
# needs passwordless sudo, tcpdump, python3, ethtool and xvfb-run.
#
# The client is dnssd: the package's, or upstream/m2-mdns/mdnsreg.c under
# that name in a tree that has the library's registering half alone. -U and
# -X are mdnsreg's only, so against the package's dnssd those two cases are
# left out.
# Exit status: 0 when every case went as it should.

set -eu

here=$(cd "$(dirname "$0")/.." && pwd)
aros=$(realpath "${1:-$here/build-aros-linux/bin/linux-x86_64/AROS}")
work=$(realpath "${2:-$(mktemp -d)}")
sys="$work/AROS"
c=SYS:Extras/aros-midihub/C
type=_regtest._udp

test -x "$aros/boot/linux/AROSBootstrap"
test -f "$aros/Extras/aros-midihub/C/dnssd"
test -f "$aros/System/Network/Services/mdnsd"
# mdnsreg says its name in its usage line
if grep -q 'mdnsreg -R' "$aros/Extras/aros-midihub/C/dnssd"; then
    own=yes
else
    own=no
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
db="$sys/System/Network/AROSTCP/db"
echo 'eth1 DEV=DEVS:networks/eth.device UNIT=1 IP=192.168.166.2 NETMASK=255.255.255.0 UP' \
    >> "$db/interfaces"
printf 'HOST 192.168.166.2 arosbox.arosnet arosbox\nDOMAIN arosnet 192.168.166.\n' \
    > "$db/netdb-myhost"
sed -i 's/OPENGUI=.*/OPENGUI=NO/' "$db/general.config"
sudo setcap cap_net_raw,cap_net_admin+ep "$sys/boot/linux/AROSBootstrap"
cat > "$sys/S/reg-run" <<RUNEOF
FailAt 21
CD SYS:System/Network/AROSTCP
Path C S ADD QUIET
Run >NIL: QUIET AROSTCP
WaitForPort AROSTCP
Wait 8
Run >SYS:reg-main.log $c/dnssd -R "Reg Test" $type . 5000 "name=Two Words" "path=/a" plain
Wait 4
$c/dnssd >SYS:reg-dup.log -R "Reg Test" $type . 5009
RUNEOF
if [ $own = yes ]; then
    cat >> "$sys/S/reg-run" <<RUNEOF
Run >SYS:reg-upd.log $c/dnssd -U "Upd Test" $type . 5001 5002 "v=2" "note=new text"
Run >SYS:reg-proxy.log $c/dnssd -P "Proxy Test" $type . 5003 printer 192.168.166.77 "k=v"
Run >SYS:reg-gone.log $c/dnssd -X "Gone Test" $type . 5004
RUNEOF
fi
cat >> "$sys/S/reg-run" <<RUNEOF
Wait 16
Break \`Status COMMAND=$c/dnssd\` C
Wait 1
Break \`Status COMMAND=$c/dnssd\` C
Wait 1
Break \`Status COMMAND=$c/dnssd\` C
Wait 3
Shutdown
RUNEOF
printf 'Run >NIL: Execute S:reg-run\n' > "$sys/S/User-Startup"

(sudo timeout 70 tcpdump -i aroshost -n -U -w "$work/mdns.pcap" udp port 5353 \
    > /dev/null 2>&1 || true) &
(cd "$sys" && timeout 80 xvfb-run -a boot/linux/AROSBootstrap \
    > "$work/serial.log" 2>&1) || true
wait || true
cp "$sys"/reg-*.log "$work/" 2> /dev/null || true

status=0
sudo chmod a+r "$work/mdns.pcap" 2> /dev/null || true
# Every record AROS sent in an answer, in the order it was sent, one to a
# line: name, type, seconds, data.
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
            p += 10
            rd = m[p:p + rdlen]
            if t == 1:
                text = '.'.join(str(b) for b in rd)
            elif t == 12:
                text = name(m, p)[0]
            elif t == 33:
                text = '%d %s' % (struct.unpack('>H', rd[4:6])[0],
                                  name(m, p + 6)[0])
            elif t == 16:
                items, q = [], 0
                while q < len(rd):
                    items.append(rd[q + 1:q + 1 + rd[q]].decode('utf-8', 'replace'))
                    q += 1 + rd[q]
                text = '|'.join(items)
            else:
                text = ''
            p += rdlen
            print(n, types.get(t, t), ttl, text)
    except (IndexError, struct.error):
        pass
PYEOF
)
echo "$sent" > "$work/sent.log"
has() { echo "$sent" | grep -qF -x "$1"; }
last() { echo "$sent" | grep -F "$1" | tail -1; }
check() {
    if [ "$1" = yes ]; then
        echo "ok: $2"
    else
        echo "FAIL: $2"
        status=1
    fi
}
yes_if() { if "$@"; then echo yes; else echo no; fi; }

t=$type.local
four=no
if has "$t PTR 4500 Reg Test.$t" &&
   has "Reg Test.$t SRV 120 5000 arosbox.local" &&
   has "Reg Test.$t TXT 4500 name=Two Words|path=/a|plain" &&
   has 'arosbox.local A 120 192.168.166.2'; then
    four=yes
fi
check $four "a service is announced with its four records, the text's items as they were given"

check "$(yes_if grep -q -- '-4' "$work/reg-dup.log")" \
    "the same name for the same type a second time is refused: $(head -1 "$work/reg-dup.log" 2> /dev/null)"

if [ $own = yes ]; then
    upd=no
    if has "Upd Test.$t SRV 120 5001 arosbox.local" &&
       has "Upd Test.$t TXT 4500 v=1" &&
       has "Upd Test.$t SRV 120 5002 arosbox.local" &&
       has "Upd Test.$t TXT 4500 v=2|note=new text" &&
       grep -q 'update: 0' "$work/reg-upd.log"; then
        upd=yes
    fi
    check $upd "a changed port and text are announced in place of the old"

    proxy=no
    if has "Proxy Test.$t SRV 120 5003 printer.local" &&
       has 'printer.local A 120 192.168.166.77'; then
        proxy=yes
    fi
    check $proxy "a service for another host names that host and its address"

    gone=yes
    case "$(last "Gone Test.$t SRV")" in
    ''|*" SRV 0 "*) ;;
    *) gone=no ;;
    esac
    check $gone "a client that ended without closing left nothing announced: $(last "Gone Test.$t SRV" | cut -d' ' -f3-5)"
    names="Reg Upd Proxy"
else
    names="Reg"
fi

bye=yes
for n in $names; do
    case "$(last "$n Test.$t SRV")" in
    *" SRV 0 "*) ;;
    *) bye=no ;;
    esac
done
check $bye "each service is withdrawn with a goodbye when its client ends"

echo "logs: $work"
exit $status
