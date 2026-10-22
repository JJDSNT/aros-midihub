#!/usr/bin/env python3
"""Exercise late mDNS startup, daemon restart and withdrawal with live Avahi.

Requires the existing eth1/aroshost veth test network, sudo/setcap/tcpdump,
Xvfb, Avahi and node-rtpmidi 1.0.0. The input image must include netmidid,
mdnsd, matching bonami.library and C:NetMIDIStatus. Only a fresh copy is used.
"""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import threading
import time

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('image', type=Path)
parser.add_argument('work', type=Path)
parser.add_argument('--stop-probe', type=Path, required=True,
                    help='native tests/review/mdns_stop_probe.c binary')
parser.add_argument('--node-modules', type=Path, required=True)
parser.add_argument('--instance', default='AROS Avahi ' + format(time.time_ns(), 'x'))
args = parser.parse_args()
out = args.work.resolve()
image = out/'AROS'
instance = args.instance
assert instance and len(instance.encode()) <= 63
assert '.' not in instance and not any(ord(c) < 32 for c in instance)
assert not image.exists(), 'refusing to overwrite an existing test image'
out.mkdir(parents=True, exist_ok=True)
shutil.copytree(args.image.resolve(), image, symlinks=True)
shutil.copy2(args.stop_probe.resolve(), image/'C/MDNSStopProbe')
db = image/'System/Network/AROSTCP/db'
with (db/'interfaces').open('a') as file:
    file.write('\neth1 DEV=DEVS:networks/eth.device UNIT=1 IP=192.168.166.2 NETMASK=255.255.255.0 UP\n')
(db/'netdb-myhost').write_text('HOST 192.168.166.2 arosbox.arosnet arosbox\nDOMAIN arosnet 192.168.166.\n')
config = db/'general.config'
config.write_text(re.sub(r'OPENGUI=.*', 'OPENGUI=NO', config.read_text()))
for name, enabled in [('mdns', 'no'), ('netmidi', 'yes')]:
    config = db/'services.d'/name
    config.write_text(re.sub(r'Enabled=.*', 'Enabled='+enabled, config.read_text()))
(image/'Prefs/Env-Archive/NetMIDI').write_text('session_name='+instance+'\n')
(image/'Prefs/Env-Archive/netmidid.log').write_text('SYS:netmidid.log')
(image/'S/review-advertise').write_text('''FailAt 21
CD SYS:System/Network/AROSTCP
Path C S ADD QUIET
Run >NIL: QUIET AROSTCP
WaitForPort AROSTCP
Wait 8
NetMIDIStatus >SYS:no-mdns-status.log
Wait 8
NetMIDIStatus >SYS:ip-only-counts.log
Run >SYS:mdns-first.log SYS:System/Network/Services/mdnsd
Wait 17
Echo >SYS:first-announcement.log ready
C:MDNSStopProbe >SYS:daemon-stop.log
Wait 3
Run >SYS:mdns-second.log SYS:System/Network/Services/mdnsd
Wait 23
Echo >SYS:second-announcement.log ready
Break `Status COMMAND=netmidi` C
Wait 4
NetMIDIStatus >SYS:stopped-status.log
Shutdown
''')
(image/'S/User-Startup').write_text('Run >NIL: Execute S:review-advertise\n')
subprocess.run(['sudo', 'setcap', 'cap_net_raw,cap_net_admin+ep',
                str(image/'boot/linux/AROSBootstrap')], check=True)

def peer():
    for _ in range(600):
        if (image/'no-mdns-status.log').exists():
            env = dict(os.environ, NODE_PATH=str(args.node_modules.resolve()))
            with (out/'peer.log').open('w') as log:
                subprocess.run(['timeout', '10', 'node',
                                str(root/'tests/interop/node_rtpmidi_peer.js'),
                                '5114', '192.168.166.2', '5004'], env=env,
                               stdout=log, stderr=subprocess.STDOUT)
            return
        time.sleep(.1)

thread = threading.Thread(target=peer, daemon=True)
thread.start()
with (out/'browse.log').open('w') as browse, (out/'serial.log').open('w') as serial:
    capture = subprocess.Popen(['sudo', 'timeout', '110', 'tcpdump', '-i', 'aroshost',
                                '-n', '-U', '-w', str(out/'mdns.pcap'), 'udp', 'port', '5353'],
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    # -t would exit after the initial list, before our late daemon starts.
    browser = subprocess.Popen(['stdbuf', '-oL', 'avahi-browse', '-rp', '_apple-midi._udp'],
                               stdout=browse, stderr=subprocess.STDOUT)
    boot = subprocess.Popen(['timeout', '110', 'xvfb-run', '-a', 'boot/linux/AROSBootstrap'],
                            cwd=image, stdout=serial, stderr=subprocess.STDOUT)
    try:
        while boot.poll() is None:
            assert browser.poll() is None, 'Avahi browser exited before the test finished'
            time.sleep(.1)
        assert boot.returncode == 0, 'AROS did not shut down successfully'
        assert browser.poll() is None, 'Avahi browser exited prematurely'
    finally:
        if boot.poll() is None:
            boot.terminate()
        browser.terminate()
        browser.wait(timeout=5)
        capture.terminate()
        capture.wait(timeout=5)
thread.join(timeout=2)
for name in ['netmidid.log', 'no-mdns-status.log', 'ip-only-counts.log', 'stopped-status.log',
             'mdns-first.log', 'mdns-second.log', 'first-announcement.log',
             'second-announcement.log', 'daemon-stop.log']:
    shutil.copy2(image/name, out/name)
serial = (out/'serial.log').read_text(errors='replace')
assert not any(signature in serial for signature in
               ['Guru Meditation', 'ALERT', 'SIGSEGV', 'tried to free', 'Trap signal', 'free-list corruption'])
assert 'session "'+instance+'"' in (out/'no-mdns-status.log').read_text()
assert 'in 4 messages in 1 packets' in (out/'ip-only-counts.log').read_text()
assert 'not running' in (out/'stopped-status.log').read_text()
assert (out/'netmidid.log').read_text().count('advertised by the system mDNS service') >= 2

events = []
for line in (out/'browse.log').read_text().splitlines():
    parts = line.split(';')
    if len(parts) < 6 or parts[1:3] != ['aroshost', 'IPv4']:
        continue
    decoded = re.sub(r'\\([0-9]{3})', lambda match: chr(int(match[1])), parts[3])
    if decoded == instance and parts[4] == '_apple-midi._udp':
        if parts[0] == '=':
            assert parts[7:9] == ['192.168.166.2', '5004'], line
        events.append(parts[0])
# Require two real add/resolve/remove cycles for this run's unique instance.
remaining = iter(events)
for expected in ['+', '=', '-', '+', '=', '-']:
    assert any(event == expected for event in remaining), (expected, events)
subprocess.run(['python3', str(root/'tests/review/mdns_advertise_capture.py'),
                str(out/'mdns.pcap'), instance], check=True)
(out/'result.json').write_text(json.dumps({'instance': instance, 'events': events,
    'address': '192.168.166.2', 'port': 5004, 'browser_alive_throughout': True}, indent=2)+'\n')
print('PASS Avahi: late discovery, correct address/port, daemon restart, rediscovery and withdrawal')
print('PASS MIDI by IP while mdnsd is absent; logs:', out)
