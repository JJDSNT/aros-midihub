"""Check two publication/withdrawal cycles in a native Ethernet pcap."""
import struct
import sys
from pathlib import Path

data = Path(sys.argv[1]).read_bytes()
assert data[:4] == b'\xd4\xc3\xb2\xa1', 'expected little-endian microsecond pcap'
records = []

def name(packet, at, depth=0):
    assert depth < 16
    labels = []
    while True:
        size = packet[at]
        at += 1
        if not size:
            return '.'.join(labels), at
        if size & 0xc0 == 0xc0:
            rest, _ = name(packet, ((size & 0x3f) << 8) | packet[at], depth + 1)
            return '.'.join(labels + [rest]), at + 1
        assert size <= 63
        labels.append(packet[at:at + size].decode('utf-8'))
        at += size

at = 24
while at + 16 <= len(data):
    sec, usec, size, _ = struct.unpack_from('<IIII', data, at)
    frame = data[at + 16:at + 16 + size]
    at += 16 + size
    if len(frame) < 42 or frame[12:14] != b'\x08\x00' or frame[23] != 17:
        continue
    if frame[26:30] != bytes([192, 168, 166, 2]):
        continue
    udp = 14 + (frame[14] & 15) * 4
    if struct.unpack_from('>H', frame, udp)[0] != 5353:
        continue
    packet = frame[udp + 8:]
    _, flags, questions, answers, authority, additional = struct.unpack_from('>6H', packet)
    if not flags & 0x8000:
        continue
    pos = 12
    for _ in range(questions):
        _, pos = name(packet, pos)
        pos += 4
    for _ in range(answers + authority + additional):
        owner, pos = name(packet, pos)
        kind, cls, ttl, size = struct.unpack_from('>HHIH', packet, pos)
        pos += 10
        value = packet[pos:pos + size]
        if kind == 12:
            value = name(packet, pos)[0]
        elif kind == 33:
            value = (struct.unpack_from('>H', packet, pos + 4)[0], name(packet, pos + 6)[0])
        records.append((sec + usec / 1000000, owner, kind, cls, ttl, value))
        pos += size

instance = (sys.argv[2] if len(sys.argv) > 2 else 'AROS Announce Test') + '._apple-midi._udp.local'
ptrs = [r for r in records if r[1] == '_apple-midi._udp.local' and r[2] == 12 and r[5] == instance]
withdrawals = [r[0] for r in ptrs if r[4] == 0]
assert len(withdrawals) >= 2, 'missing daemon-stop/service-stop goodbyes'
assert any(r[4] > 0 and r[0] < withdrawals[0] for r in ptrs), 'missing initial publication'
assert any(r[4] > 0 and withdrawals[0] < r[0] < withdrawals[-1] for r in ptrs), 'missing publication after daemon restart'
assert all(r[3] == 1 for r in ptrs), 'shared PTR must not carry cache-flush'
srvs = [r for r in records if r[1] == instance and r[2] == 33 and r[4] > 0]
assert len(srvs) >= 2 and all(r[5] == (5004, 'arosbox.local') for r in srvs)
assert any(r[1] == instance and r[2] == 16 and r[4] > 0 and r[5] == b'\x00' for r in records)
assert any(r[1] == 'arosbox.local' and r[2] == 1 and r[4] > 0 and r[5] == bytes([192, 168, 166, 2]) for r in records)
print('PASS: exact instance, PTR/SRV/TXT/A, address/port, daemon restart, two TTL-zero withdrawals')
