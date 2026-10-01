"""Exercise two MIDIHub processes over localhost without AROS dependencies."""

import os
import selectors
import signal
import socket
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


def main():
    binary = sys.argv[1]
    run_case(binary, "--probe-note", ("bytes=903c64", "bytes=903c00"))
    run_case(binary, "--probe-sysex",
             ("SysEx complete bytes=4", "SysEx complete bytes=2004"))
    run_case(binary, None, (), use_config=True)
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
