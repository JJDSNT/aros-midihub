#!/usr/bin/env python3
"""Add the AROS/OS4 icOn metadata chunk to a PNG Workbench tool icon."""

import argparse
import binascii
import struct
from pathlib import Path

PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"
ICON_CHUNK = b"icOn"
ICONA_TYPE = 0x8000100F
ICONA_STACK_SIZE = 0x80001009
WBTOOL = 3


def chunk(kind: bytes, payload: bytes) -> bytes:
    body = kind + payload
    return struct.pack(">I", len(payload)) + body + struct.pack(">I", binascii.crc32(body) & 0xFFFFFFFF)


def make_icon(source: Path, destination: Path, selected: Path | None) -> None:
    data = source.read_bytes()
    if not data.startswith(PNG_SIGNATURE):
        raise ValueError(f"{source} is not a PNG")
    offset = len(PNG_SIGNATURE)
    output = bytearray(PNG_SIGNATURE)
    metadata = struct.pack(">IIII", ICONA_TYPE, WBTOOL, ICONA_STACK_SIZE, 16384)
    inserted = False
    while offset < len(data):
        if offset + 12 > len(data):
            raise ValueError("truncated PNG chunk")
        length = struct.unpack_from(">I", data, offset)[0]
        end = offset + 12 + length
        if end > len(data):
            raise ValueError("truncated PNG payload")
        kind = data[offset + 4 : offset + 8]
        if kind == b"IEND" and not inserted:
            output.extend(chunk(ICON_CHUNK, metadata))
            inserted = True
        elif kind == ICON_CHUNK:
            offset = end
            continue
        output.extend(data[offset:end])
        offset = end
    if not inserted:
        raise ValueError("PNG has no IEND chunk")
    if selected is not None:
        selected_data = selected.read_bytes()
        if not selected_data.startswith(PNG_SIGNATURE):
            raise ValueError(f"{selected} is not a PNG")
        output.extend(selected_data)
    destination.write_bytes(output)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    parser.add_argument("selected", type=Path, nargs="?")
    args = parser.parse_args()
    make_icon(args.source, args.destination, args.selected)


if __name__ == "__main__":
    main()
