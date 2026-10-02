"""Exercise two MIDIHub processes over localhost without AROS dependencies."""

import os
import selectors
import signal
import socket
import struct
import subprocess
import sys
import tempfile
import time
from pathlib import Path


def free_port_pair():
    for _ in range(100):
        first = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        first.bind(("127.0.0.1", 0))
        port = first.getsockname()[1]
        if port > 65531:
            first.close()
            continue
        sockets = [first]
        try:
            for offset in (1, 2, 3):
                item = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                sockets.append(item)
                item.bind(("127.0.0.1", port + offset))
        except OSError:
            pass
        else:
            for item in sockets:
                item.close()
            return port
        for item in sockets:
            item.close()
    raise RuntimeError("no free consecutive UDP ports")


def run_case(binary, probe, expected, use_config=False):
    port = free_port_pair()
    directory = tempfile.TemporaryDirectory() if use_config else None
    if use_config:
        root = Path(directory.name)
        receiver_config = root / "receiver.conf"
        sender_config = root / "sender.conf"
        receiver_config.write_text(
            f"local_port={port + 2}\nsession_name=AROS Receiver\n"
        )
        sender_config.write_text(
            f"local_port={port}\npeer_ip=127.0.0.1\n"
            f"peer_port={port + 2}\nsession_name=AROS Sender\n"
        )
        receiver_args = [binary, "--config", str(receiver_config)]
        sender_args = [binary, "--config", str(sender_config)]
    else:
        receiver_args = [binary, str(port + 2)]
        sender_args = [binary, str(port), "127.0.0.1", str(port + 2), probe]
    receiver = subprocess.Popen(
        receiver_args, stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT, bufsize=0
    )
    sender = None
    selector = selectors.DefaultSelector()
    selector.register(receiver.stdout, selectors.EVENT_READ, "receiver")
    seen = {"receiver": [], "sender": []}
    pending = {"receiver": b"", "sender": b""}
    try:
        sender = subprocess.Popen(
            sender_args,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            bufsize=0
        )
        selector.register(sender.stdout, selectors.EVENT_READ, "sender")
        deadline = time.monotonic() + 8
        while time.monotonic() < deadline:
            for key, _ in selector.select(timeout=0.5):
                chunk = os.read(key.fileobj.fileno(), 4096)
                pending[key.data] += chunk
                while b"\n" in pending[key.data]:
                    line, pending[key.data] = pending[key.data].split(b"\n", 1)
                    seen[key.data].append(line.decode(errors="replace").strip())
            sender_log = "\n".join(seen["sender"])
            receiver_log = "\n".join(seen["receiver"])
            if ("session connected" in sender_log and
                    "clock exchange completed" in sender_log and
                    "session connected" in receiver_log and
                    all(item in receiver_log for item in expected)):
                return
            if sender.poll() is not None or receiver.poll() is not None:
                break
        raise AssertionError(f"sender: {seen['sender']}\nreceiver: {seen['receiver']}")
    finally:
        selector.close()
        for process in (sender, receiver):
            if process is not None:
                process.send_signal(signal.SIGINT)
                try:
                    process.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
        if directory is not None:
            directory.cleanup()


def run_feedback_case(binary):
    """Use a raw AppleMIDI peer to inspect control-port receiver feedback."""
    port = free_port_pair()
    control = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    data = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    control.bind(("127.0.0.1", port + 2))
    data.bind(("127.0.0.1", port + 3))
    control.settimeout(0.2)
    data.settimeout(2)
    log = tempfile.TemporaryFile(mode="w+b")
    process = subprocess.Popen(
        [binary, str(port)], stdout=log, stderr=subprocess.STDOUT
    )
    token = 0x12345678
    peer_ssrc = 0x89ABCDEF
    invite = b"\xff\xffIN" + struct.pack(">III", 2, token, peer_ssrc) + b"Peer\0"
    try:
        deadline = time.monotonic() + 3
        while True:
            control.sendto(invite, ("127.0.0.1", port))
            try:
                reply, _ = control.recvfrom(128)
                break
            except socket.timeout:
                if time.monotonic() >= deadline:
                    raise AssertionError("control invitation was not accepted")
        assert reply[:4] == b"\xff\xffOK" and reply[8:12] == invite[8:12]
        data.sendto(invite, ("127.0.0.1", port + 1))
        reply, _ = data.recvfrom(128)
        assert reply[:4] == b"\xff\xffOK" and reply[8:12] == invite[8:12]
        for sequence, payload, expected_ack in (
            (0xffff, b"\x03\x90\x3c\x64", 0xffff),
            (0, b"\x40\x00\x00\x00", 0x10000),
        ):
            packet = struct.pack(">BBHII", 0x80, 0xe1, sequence,
                                 100, peer_ssrc) + payload
            data.sendto(packet, ("127.0.0.1", port + 1))
            control.settimeout(2)
            feedback, source = control.recvfrom(128)
            assert source[1] == port
            assert feedback[:4] == b"\xff\xffRS" and len(feedback) == 12
            assert struct.unpack(">I", feedback[8:12])[0] == expected_ack
            if sequence == 0xffff:
                data.sendto(packet, ("127.0.0.1", port + 1))
                repeated, _ = control.recvfrom(128)
                assert repeated == feedback
        invalid = struct.pack(">BBHII", 0x80, 0xe1, 1, 100, peer_ssrc)
        data.sendto(invalid + b"\x40\x80\x00", ("127.0.0.1", port + 1))
        control.settimeout(0.2)
        try:
            control.recvfrom(128)
        except socket.timeout:
            pass
        else:
            raise AssertionError("malformed journal was acknowledged")
        gap = struct.pack(">BBHII", 0x80, 0xe1, 2, 100, peer_ssrc)
        data.sendto(gap + b"\x40\x80\x00\x00",
                    ("127.0.0.1", port + 1))
        control.settimeout(2)
        feedback, _ = control.recvfrom(128)
        assert struct.unpack(">I", feedback[8:12])[0] == 0x10002
        note_on = struct.pack(">BBHII", 0x80, 0xe1, 3, 100, peer_ssrc)
        data.sendto(note_on + b"\x03\x90\x3c\x64",
                    ("127.0.0.1", port + 1))
        feedback, _ = control.recvfrom(128)
        assert struct.unpack(">I", feedback[8:12])[0] == 0x10003
        recovery = struct.pack(">BBHII", 0x80, 0xe1, 5, 100, peer_ssrc)
        journal = b"\x20\x00\x04\x00\x06\x08\x00\x77\x08"
        data.sendto(recovery + b"\x40" + journal,
                    ("127.0.0.1", port + 1))
        feedback, _ = control.recvfrom(128)
        assert struct.unpack(">I", feedback[8:12])[0] == 0x10005
        note_on = struct.pack(">BBHII", 0x80, 0xe1, 6, 100, peer_ssrc)
        data.sendto(note_on + b"\x03\x90\x3c\x64",
                    ("127.0.0.1", port + 1))
        feedback, _ = control.recvfrom(128)
        assert struct.unpack(">I", feedback[8:12])[0] == 0x10006
        safe = struct.pack(">BBHII", 0x80, 0xe1, 8, 100, peer_ssrc)
        safe_journal = b"\x20\x00\x07\x00\x06\x08\x80\x77\x08"
        data.sendto(safe + b"\x40" + safe_journal,
                    ("127.0.0.1", port + 1))
        feedback, _ = control.recvfrom(128)
        assert struct.unpack(">I", feedback[8:12])[0] == 0x10008
        note_off = struct.pack(">BBHII", 0x80, 0xe1, 9, 100, peer_ssrc)
        data.sendto(note_off + b"\x03\x80\x3c\x00",
                    ("127.0.0.1", port + 1))
        feedback, _ = control.recvfrom(128)
        assert struct.unpack(">I", feedback[8:12])[0] == 0x10009
        t1 = int(time.time() * 10000)
        ck0 = struct.pack(">4sIB3xQQQ", b"\xff\xffCK", peer_ssrc,
                          0, t1, 0, 0)
        data.sendto(ck0, ("127.0.0.1", port + 1))
        ck1, _ = data.recvfrom(128)
        assert ck1[:4] == b"\xff\xffCK" and ck1[8] == 1
        t2 = struct.unpack(">Q", ck1[20:28])[0]
        ck2 = struct.pack(">4sIB3xQQQ", b"\xff\xffCK", peer_ssrc,
                          2, t1, t2, int(time.time() * 10000))
        data.sendto(ck2, ("127.0.0.1", port + 1))
        future = int(time.time() * 10000) + 3000
        missed_on = struct.pack(">BBHII", 0x80, 0xe1, 11,
                                future & 0xffffffff, peer_ssrc)
        on_journal = b"\x20\x00\x0a\x00\x07\x08\x01\xf1\x3c\xe4"
        data.sendto(missed_on + b"\x40" + on_journal,
                    ("127.0.0.1", port + 1))
        feedback, _ = control.recvfrom(128)
        assert struct.unpack(">I", feedback[8:12])[0] == 0x1000B
        deadline = time.monotonic() + 1
        while True:
            log.seek(0)
            output = log.read().decode(errors="replace")
            if "recovered Note On channel=0 note=60 velocity=100" in output:
                break
            if time.monotonic() >= deadline:
                raise AssertionError(output)
            time.sleep(0.01)
        state_packet = struct.pack(">BBHII", 0x80, 0xe1, 13,
                                   future & 0xffffffff, peer_ssrc)
        state_journal = (
            b"\x20\x00\x0c\x00\x08\x90\x05\x82\x03\x01\x20"
        )
        data.sendto(state_packet + b"\x40" + state_journal,
                    ("127.0.0.1", port + 1))
        feedback, _ = control.recvfrom(128)
        assert struct.unpack(">I", feedback[8:12])[0] == 0x1000D
        deadline = time.monotonic() + 1
        while True:
            log.seek(0)
            output = log.read().decode(errors="replace")
            if ("recovered Program Change channel=0 program=5" in output and
                    "recovered Pitch Bend channel=0 value=4097" in output):
                break
            if time.monotonic() >= deadline:
                raise AssertionError(output)
            time.sleep(0.01)
        control_packet = struct.pack(">BBHII", 0x80, 0xe1, 15,
                                     future & 0xffffffff, peer_ssrc)
        control_journal = b"\x20\x00\x0e\x00\x06\x40\x00\x07\x5a"
        data.sendto(control_packet + b"\x40" + control_journal,
                    ("127.0.0.1", port + 1))
        feedback, _ = control.recvfrom(128)
        assert struct.unpack(">I", feedback[8:12])[0] == 0x1000F
        deadline = time.monotonic() + 1
        while True:
            log.seek(0)
            output = log.read().decode(errors="replace")
            if "recovered Control Change channel=0 controller=7 value=90" in output:
                break
            if time.monotonic() >= deadline:
                raise AssertionError(output)
            time.sleep(0.01)
        sustain_on = struct.pack(">BBHII", 0x80, 0xe1, 16,
                                 future & 0xffffffff, peer_ssrc)
        data.sendto(sustain_on + b"\x03\xb0\x40\x7f",
                    ("127.0.0.1", port + 1))
        feedback, _ = control.recvfrom(128)
        assert struct.unpack(">I", feedback[8:12])[0] == 0x10010
        sustain_gap = struct.pack(">BBHII", 0x80, 0xe1, 18,
                                  future & 0xffffffff, peer_ssrc)
        sustain_journal = b"\x20\x00\x11\x00\x06\x40\x00\x40\x83"
        data.sendto(sustain_gap + b"\x40" + sustain_journal,
                    ("127.0.0.1", port + 1))
        feedback, _ = control.recvfrom(128)
        assert struct.unpack(">I", feedback[8:12])[0] == 0x10012
        deadline = time.monotonic() + 1
        while True:
            log.seek(0)
            output = log.read().decode(errors="replace")
            if "recovered Sustain toggle channel=0 count=3" in output:
                break
            if time.monotonic() >= deadline:
                raise AssertionError(output)
            time.sleep(0.01)
        last_note = struct.pack(">BBHII", 0x80, 0xe1, 19,
                                future & 0xffffffff, peer_ssrc)
        data.sendto(last_note + b"\x03\x90\x3e\x64",
                    ("127.0.0.1", port + 1))
        feedback, _ = control.recvfrom(128)
        assert struct.unpack(">I", feedback[8:12])[0] == 0x10013
        all_notes_gap = struct.pack(">BBHII", 0x80, 0xe1, 21,
                                    future & 0xffffffff, peer_ssrc)
        all_notes_journal = b"\x20\x00\x14\x00\x06\x40\x00\x7b\xc1"
        data.sendto(all_notes_gap + b"\x40" + all_notes_journal,
                    ("127.0.0.1", port + 1))
        feedback, _ = control.recvfrom(128)
        assert struct.unpack(">I", feedback[8:12])[0] == 0x10015
        deadline = time.monotonic() + 1
        while True:
            log.seek(0)
            output = log.read().decode(errors="replace")
            if "recovered controller count channel=0 controller=123 count=1" in output:
                break
            if time.monotonic() >= deadline:
                raise AssertionError(output)
            time.sleep(0.01)
        fresh_on = struct.pack(">BBHII", 0x80, 0xe1, 22,
                               int(time.time() * 10000) & 0xffffffff,
                               peer_ssrc)
        data.sendto(fresh_on + b"\x03\x90\x3c\x64",
                    ("127.0.0.1", port + 1))
        feedback, _ = control.recvfrom(128)
        assert struct.unpack(">I", feedback[8:12])[0] == 0x10016
        pressure_gap = struct.pack(">BBHII", 0x80, 0xe1, 24,
                                   int(time.time() * 10000) & 0xffffffff,
                                   peer_ssrc)
        pressure_journal = b"\x20\x00\x17\x00\x07\x03\x28\x00\x3c\x32"
        data.sendto(pressure_gap + b"\x40" + pressure_journal,
                    ("127.0.0.1", port + 1))
        feedback, _ = control.recvfrom(128)
        assert struct.unpack(">I", feedback[8:12])[0] == 0x10018
        deadline = time.monotonic() + 1
        while True:
            log.seek(0)
            output = log.read().decode(errors="replace")
            if ("recovered Channel Aftertouch channel=0 pressure=40" in output and
                    "recovered Poly Aftertouch channel=0 note=60 pressure=50" in output):
                break
            if time.monotonic() >= deadline:
                raise AssertionError(output)
            time.sleep(0.01)
        system_gap = struct.pack(">BBHII", 0x80, 0xe1, 26,
                                 int(time.time() * 10000) & 0xffffffff,
                                 peer_ssrc)
        system_journal = (
            b"\x40\x00\x19"
            b"\x60\x07\x70\x01\x01\x09\x01"
        )
        data.sendto(system_gap + b"\x40" + system_journal,
                    ("127.0.0.1", port + 1))
        feedback, _ = control.recvfrom(128)
        assert struct.unpack(">I", feedback[8:12])[0] == 0x1001A
        deadline = time.monotonic() + 1
        while True:
            log.seek(0)
            output = log.read().decode(errors="replace")
            if ("recovered System Reset" in output and
                    "recovered Tune Request" in output and
                    "recovered Song Select song=9" in output and
                    "recovered Active Sense" in output):
                break
            if time.monotonic() >= deadline:
                raise AssertionError(output)
            time.sleep(0.01)
        sequencer_gap = struct.pack(">BBHII", 0x80, 0xe1, 28,
                                    int(time.time() * 10000) & 0xffffffff,
                                    peer_ssrc)
        sequencer_journal = b"\x40\x00\x1b\x10\x05\x10\x00\x06"
        data.sendto(sequencer_gap + b"\x40" + sequencer_journal,
                    ("127.0.0.1", port + 1))
        feedback, _ = control.recvfrom(128)
        assert struct.unpack(">I", feedback[8:12])[0] == 0x1001C
        deadline = time.monotonic() + 1
        while True:
            log.seek(0)
            output = log.read().decode(errors="replace")
            if ("recovered sequencer state running=0 clock=6 downbeat=0"
                    in output):
                break
            if time.monotonic() >= deadline:
                raise AssertionError(output)
            time.sleep(0.01)

        start_gap = struct.pack(">BBHII", 0x80, 0xe1, 30,
                                int(time.time() * 10000) & 0xffffffff,
                                peer_ssrc)
        start_journal = b"\x40\x00\x1d\x10\x03\x40"
        data.sendto(start_gap + b"\x40" + start_journal,
                    ("127.0.0.1", port + 1))
        feedback, _ = control.recvfrom(128)
        assert struct.unpack(">I", feedback[8:12])[0] == 0x1001E
        deadline = time.monotonic() + 1
        while True:
            log.seek(0)
            output = log.read().decode(errors="replace")
            if "recovered sequencer Start" in output:
                break
            if time.monotonic() >= deadline:
                raise AssertionError(output)
            time.sleep(0.01)

        stop_gap = struct.pack(">BBHII", 0x80, 0xe1, 32,
                               int(time.time() * 10000) & 0xffffffff,
                               peer_ssrc)
        stop_journal = b"\x40\x00\x1f\x10\x03\x00"
        data.sendto(stop_gap + b"\x40" + stop_journal,
                    ("127.0.0.1", port + 1))
        feedback, _ = control.recvfrom(128)
        assert struct.unpack(">I", feedback[8:12])[0] == 0x10020
        deadline = time.monotonic() + 1
        while True:
            log.seek(0)
            output = log.read().decode(errors="replace")
            if ("recovered sequencer state running=0 clock=0 downbeat=0"
                    in output):
                break
            if time.monotonic() >= deadline:
                raise AssertionError(output)
            time.sleep(0.01)
        assert "malformed recovery journal discarded" in output
        assert "1 RTP packet(s) lost; journal covers gap" in output
        assert "recovered Note Off channel=0 note=60" in output
        assert output.count("recovered Note Off channel=0 note=60") == 1
    finally:
        process.send_signal(signal.SIGINT)
        try:
            process.wait(timeout=2)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
        control.close()
        data.close()
        log.close()


def run_outgoing_journal_case(binary):
    """Inspect journals sent to a raw AppleMIDI peer."""
    port = free_port_pair()
    control = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    data = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    control.bind(("127.0.0.1", port + 2))
    data.bind(("127.0.0.1", port + 3))
    control.settimeout(3)
    data.settimeout(3)
    process = subprocess.Popen(
        [binary, str(port), "127.0.0.1", str(port + 2), "--probe-note"],
        stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT
    )
    peer_ssrc = 0x11223344
    try:
        invitation, _ = control.recvfrom(128)
        assert invitation[:4] == b"\xff\xffIN"
        accepted = (b"\xff\xffOK" + struct.pack(">I", 2) +
                    invitation[8:12] + struct.pack(">I", peer_ssrc) +
                    b"Peer\0")
        control.sendto(accepted, ("127.0.0.1", port))
        invitation, _ = data.recvfrom(128)
        assert invitation[:4] == b"\xff\xffIN"
        data.sendto(accepted, ("127.0.0.1", port + 1))
        ck0, _ = data.recvfrom(128)
        assert ck0[:4] == b"\xff\xffCK" and ck0[8] == 0
        t1 = struct.unpack(">Q", ck0[12:20])[0]
        ck1 = struct.pack(">4sIB3xQQQ", b"\xff\xffCK", peer_ssrc,
                          1, t1, int(time.time() * 10000), 0)
        data.sendto(ck1, ("127.0.0.1", port + 1))
        ck2, _ = data.recvfrom(128)
        assert ck2[:4] == b"\xff\xffCK" and ck2[8] == 2
        first, _ = data.recvfrom(128)
        assert first[:2] == b"\x80\xe1" and first[12] & 0x40
        first_sequence = struct.unpack(">H", first[2:4])[0]
        assert first[13:16] == b"\x90\x3c\x64"
        assert first[16:] == struct.pack(">BH", 0x80, first_sequence)
        second, _ = data.recvfrom(128)
        assert second[:2] == b"\x80\xe1" and second[12] & 0x40
        assert struct.unpack(">H", second[2:4])[0] == (first_sequence + 1) & 0xffff
        assert second[13:16] == b"\x90\x3c\x00"
        journal = second[16:]
        assert journal[0] & 0x20 and journal[1:3] == first[2:4]
        assert journal[5] == 0x08 and journal[6] & 0x7f == 1
        assert journal[8] & 0x7f == 60 and journal[9] & 0x7f == 100
        for _ in range(1):
            ck0, _ = data.recvfrom(128)
            assert ck0[:4] == b"\xff\xffCK" and ck0[8] == 0
            t1 = struct.unpack(">Q", ck0[12:20])[0]
            ck1 = struct.pack(">4sIB3xQQQ", b"\xff\xffCK", peer_ssrc,
                              1, t1, int(time.time() * 10000), 0)
            data.sendto(ck1, ("127.0.0.1", port + 1))
            ck2, _ = data.recvfrom(128)
            assert ck2[:4] == b"\xff\xffCK" and ck2[8] == 2
            assert struct.unpack(">Q", ck2[12:20])[0] == t1
        guard, _ = data.recvfrom(128)
        assert guard[:2] == b"\x80\xe1" and guard[12] == 0x40
        assert struct.unpack(">H", guard[2:4])[0] == (first_sequence + 2) & 0xffff
        assert guard[13] & 0x20 and guard[14:16] == first[2:4]
        feedback = b"\xff\xffRS" + struct.pack(">II", peer_ssrc,
                                                  (first_sequence + 2) & 0xffff)
        control.sendto(feedback, ("127.0.0.1", port))
        ck0, _ = data.recvfrom(128)
        assert ck0[:4] == b"\xff\xffCK" and ck0[8] == 0
        t1 = struct.unpack(">Q", ck0[12:20])[0]
        ck1 = struct.pack(">4sIB3xQQQ", b"\xff\xffCK", peer_ssrc,
                          1, t1, int(time.time() * 10000), 0)
        data.sendto(ck1, ("127.0.0.1", port + 1))
        ck2, _ = data.recvfrom(128)
        assert ck2[:4] == b"\xff\xffCK" and ck2[8] == 2
        data.settimeout(0.3)
        try:
            unexpected, _ = data.recvfrom(128)
        except socket.timeout:
            pass
        else:
            raise AssertionError(f"unexpected packet after startup sync: {unexpected!r}")
    finally:
        process.send_signal(signal.SIGINT)
        try:
            process.wait(timeout=2)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
        control.close()
        data.close()


def run_mdns_case(binary):
    """Resolve MIDIHub's AppleMIDI service over a local DNS-SD query."""
    port = free_port_pair()
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("127.0.0.1", 0))
    sock.settimeout(0.2)
    query = (b"\x12\x34\x00\x00\x00\x01\x00\x00\x00\x00\x00\x00"
             b"\x0b_apple-midi\x04_udp\x05local\x00\x00\x0c\x00\x01")
    process = subprocess.Popen(
        [binary, str(port)], stdout=subprocess.DEVNULL,
        stderr=subprocess.STDOUT
    )
    try:
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline:
            sock.sendto(query, ("127.0.0.1", 5353))
            try:
                packet, source = sock.recvfrom(1024)
            except socket.timeout:
                continue
            if (packet[:2] != b"\x12\x34" or b"AROS MIDIHub" not in packet):
                continue
            assert source[1] == 5353
            assert packet[2:4] == b"\x84\x00"
            assert packet[6:8] == b"\x00\x04"
            assert struct.pack(">H", port) in packet
            break
        else:
            raise AssertionError("MIDIHub did not answer AppleMIDI DNS-SD query")
    finally:
        process.send_signal(signal.SIGINT)
        try:
            process.wait(timeout=2)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
        sock.close()


def main():
    binary = sys.argv[1]
    run_case(binary, "--probe-note", ("bytes=903c64", "bytes=903c00"))
    run_case(binary, "--probe-sysex",
             ("SysEx complete bytes=4", "SysEx complete bytes=2004"))
    run_case(binary, None, (), use_config=True)
    run_feedback_case(binary)
    run_outgoing_journal_case(binary)
    run_mdns_case(binary)
    with tempfile.TemporaryDirectory() as directory:
        bad_config = Path(directory) / "invalid.conf"
        bad_config.write_text("local_port=65535\n")
        result = subprocess.run(
            [binary, "--config", str(bad_config)],
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            text=True, timeout=2
        )
        assert result.returncode != 0 and "configuration" in result.stdout
    print("network loopback passed")


if __name__ == "__main__":
    main()
