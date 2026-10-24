#!/bin/sh
# Checks the mDNS command-line tools (workbench/network/mdns/tools: dnssd,
# Nearby, Share, Handshake) on Linux-hosted AROS against the host's Avahi,
# with mdnsd's queued discovery and its listing of service types.
#
#   tools/mdns-tools-hosted.sh AROS_DIR [WORK_DIR]
#
# AROS_DIR is a hosted system with mdnsd, bonami.library and the four tools
# in C: of the revision under test. eth1 is AROS's (192.168.166.2),
# aroshost the host's (192.168.166.1). It needs passwordless sudo, ethtool,
# xvfb-run, a running avahi-daemon and avahi-utils.
#
# The host advertises "Host Web" (_http._tcp, port 8080, path=/) and "Host
# SSH" (_ssh._tcp, port 22). On AROS:
#   dnssd -V                           the library's version
#   dnssd -B _services._dns-sd._udp    both types listed
#   dnssd -B _http._tcp                the instance, address, port and text
#   dnssd -Z _http._tcp                valid PTR/TXT records; address and port
#                                      in a comment (the API has no SRV target)
#   dnssd -L, Handshake                "Host Web" resolved
#   dnssd -L of a name nobody has      not resolved, after mdnsd gives up
#   Nearby (no type), Nearby web       the kinds, and the web server
#   Share, dnssd -R                    seen by the host's Avahi, text included
# Exit status: 0 when all of it was seen.
set -eu

here=$(cd "$(dirname "$0")/.." && pwd)
aros=$(realpath "$1")
work=$(realpath "${2:-$(mktemp -d)}")
sys="$work/AROS"

test -x "$aros/boot/linux/AROSBootstrap"
for tool in dnssd Nearby Share Handshake; do
    test -f "$aros/C/$tool"
done
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

mkdir -p "$work"
sudo rm -rf "$sys"
cp -a --reflink=auto "$aros" "$sys"
db="$sys/System/Network/AROSTCP/db"
echo 'eth1 DEV=DEVS:networks/eth.device UNIT=1 IP=192.168.166.2 NETMASK=255.255.255.0 UP' \
    >> "$db/interfaces"
printf 'HOST 192.168.166.2 arosbox.arosnet arosbox\nDOMAIN arosnet 192.168.166.\n' \
    > "$db/netdb-myhost"
sed -i 's/OPENGUI=.*/OPENGUI=NO/' "$db/general.config"
sudo setcap cap_net_raw,cap_net_admin+ep "$sys/boot/linux/AROSBootstrap"
cat > "$sys/S/tools-run" <<'RUNEOF'
FailAt 21
CD SYS:System/Network/AROSTCP
Path C S ADD QUIET
Run >NIL: QUIET AROSTCP
WaitForPort AROSTCP
Wait 10
C:dnssd >SYS:version.log -V
Run >SYS:types.log C:dnssd -B _services._dns-sd._udp
Run >SYS:browse.log C:dnssd -B _http._tcp
Run >SYS:zone.log C:dnssd -Z _http._tcp
C:Nearby >SYS:kinds.log TIME 8
C:Nearby >SYS:web.log web PORTS TIME 6
C:dnssd >SYS:resolve.log -L "Host Web" _http._tcp
C:Handshake >SYS:handshake.log "Host Web" web TIME 10
C:dnssd >SYS:missing.log -L "Nobody Here" _http._tcp
C:dnssd >SYS:bad-port.log -R "Bad Port" _http._tcp . 70000
C:dnssd >SYS:bad-port-tail.log -R "Bad Port" _http._tcp . 80x
C:dnssd >SYS:bad-ttl.log -R "Bad TTL" _http._tcp . 8082 -t 60x
C:dnssd >SYS:bad-txt.log -R "Bad TXT" _http._tcp . 8082 "k=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
C:Share >SYS:bad-share-port.log "Bad Share" web 0
Run >SYS:share.log C:Share AmigaShare web 8081 "path=/x" "note=a b"
Run >SYS:register.log C:dnssd -R "AROS Printer" _ipp._tcp . 631 rp=printer
Wait 14
Break `Status COMMAND=dnssd` C
Wait 1
Break `Status COMMAND=dnssd` C
Wait 1
Break `Status COMMAND=dnssd` C
Wait 1
Break `Status COMMAND=dnssd` C
Break `Status COMMAND=Share` C
Wait 3
Shutdown
RUNEOF
printf 'Run >NIL: Execute S:tools-run\n' > "$sys/S/User-Startup"

# Keep the packets when Avahi sees a PTR but cannot resolve the instance
# (mdns backlog MD20); that failure cannot be diagnosed from either side's
# application log alone.
(sudo timeout 90 tcpdump -i aroshost -n -l -v udp port 5353 \
    > "$work/mdns-capture.txt" 2> /dev/null || true) &
(
    for attempt in $(seq 1 60); do
        ping -c 1 -W 1 192.168.166.2 > /dev/null 2>&1 && break
        sleep 1
    done
    timeout 75 avahi-publish -s "Host Web" _http._tcp 8080 "path=/" \
        > "$work/publish-web.log" 2>&1 &
    timeout 75 avahi-publish -s "Host SSH" _ssh._tcp 22 \
        > "$work/publish-ssh.log" 2>&1 &
    timeout 75 avahi-browse -rpk _http._tcp > "$work/avahi-http.log" 2>&1 &
    timeout 75 avahi-browse -rpk _ipp._tcp > "$work/avahi-ipp.log" 2>&1 &
    wait
) &
(cd "$sys" && timeout 150 xvfb-run -a boot/linux/AROSBootstrap \
    > "$work/serial.log" 2>&1) || true
wait || true
for log in version types browse zone kinds web resolve handshake missing \
           bad-port bad-port-tail bad-ttl bad-txt bad-share-port share \
           register; do
    cp "$sys/$log.log" "$work/" 2> /dev/null || true
done

status=0
check() {
    if eval "$2"; then
        echo "ok: $1"
    else
        echo "FAIL: $1"
        status=1
    fi
}
check "dnssd -V names bonami.library 41" \
    "grep -q '^bonami.library 41\\.' '$work/version.log'"
check "dnssd -B _services._dns-sd._udp listed _http._tcp and _ssh._tcp" \
    "grep -q '^TYPE  *_http._tcp' '$work/types.log' && grep -q '^TYPE  *_ssh._tcp' '$work/types.log'"
check "dnssd -B _http._tcp saw Host Web at 192.168.166.1 port 8080 with path=/" \
    "grep '^ADD  *Host Web  *_http._tcp' '$work/browse.log' | grep -q '192.168.166.1  *8080  *path=/'"
check "dnssd -Z gave valid PTR/TXT lines without inventing an SRV target or A owner" \
    "grep -qP '^_http._tcp.local.\\tPTR\\tHost Web._http._tcp.local.' '$work/zone.log' && grep -qP '^Host Web._http._tcp.local.\\tTXT\\tpath=/' '$work/zone.log' && grep -q '^; Host Web._http._tcp.local.: port 8080, address 192.168.166.1; SRV target unavailable$' '$work/zone.log' && ! grep -qP '\\t(SRV|A)\\t' '$work/zone.log'"
check "Nearby with no type listed the web server and SSH kinds" \
    "grep -q 'Web server (HTTP)' '$work/kinds.log' && grep -q 'SSH login' '$work/kinds.log'"
check "Nearby web PORTS showed Host Web, port 8080 and its info" \
    "grep -q 'Host Web  *port 8080  (192.168.166.1)' '$work/web.log' && grep -q 'info: path=/' '$work/web.log'"
check "dnssd -L resolved Host Web" \
    "grep '^RESOLVED Host Web' '$work/resolve.log' | grep -q '192.168.166.1  *8080'"
check "Handshake reached Host Web" \
    "grep -q 'IP:    192.168.166.1' '$work/handshake.log' && grep -q 'Port:  8080' '$work/handshake.log'"
check "dnssd -L of an absent name said it was not resolved" \
    "grep -q '^Not resolved' '$work/missing.log'"
check "dnssd rejected an overflowing port instead of wrapping it" \
    "grep -q '^dnssd: port must be 1-65535' '$work/bad-port.log'"
check "dnssd rejected trailing characters in a port" \
    "grep -q '^dnssd: port must be 1-65535' '$work/bad-port-tail.log'"
check "dnssd rejected trailing characters in a TTL" \
    "grep -q '^dnssd: TTL must be 1-4500 seconds' '$work/bad-ttl.log'"
check "dnssd rejected a TXT record instead of silently truncating it" \
    "grep -q '^dnssd: TXT record is too long' '$work/bad-txt.log'"
check "Share rejected port zero before registration" \
    "grep -q '^Share: PORT must be 1-65535' '$work/bad-share-port.log'"
check "the host's Avahi saw Share's AmigaShare with its text, a space kept" \
    "grep '^=;aroshost;IPv4;AmigaShare;_http._tcp;' '$work/avahi-http.log' | grep -q '192.168.166.2;8081;.*\"note=a b\"' && grep '^=;aroshost;IPv4;AmigaShare;_http._tcp;' '$work/avahi-http.log' | grep -q '\"path=/x\"'"
check "the host's Avahi saw dnssd -R's AROS Printer" \
    "grep '^=;aroshost;IPv4;AROS\\\\032Printer;_ipp._tcp;' '$work/avahi-ipp.log' | grep -q '192.168.166.2;631;\"rp=printer\"'"
echo "logs: $work"
exit $status
