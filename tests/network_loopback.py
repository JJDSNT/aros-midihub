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
        log.seek(0)
        output = log.read().decode(errors="replace")
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


def main():
    binary = sys.argv[1]
    run_case(binary, "--probe-note", ("bytes=903c64", "bytes=903c00"))
    run_case(binary, "--probe-sysex",
             ("SysEx complete bytes=4", "SysEx complete bytes=2004"))
    run_case(binary, None, (), use_config=True)
    run_feedback_case(binary)
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
