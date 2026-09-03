#!/usr/bin/env python3
"""Give each logical Ogg stream a stable serial number and repair page CRCs."""

import pathlib
import sys


def crc(page: bytearray) -> int:
    value = 0
    for byte in page:
        value ^= byte << 24
        for _ in range(8):
            value = ((value << 1) ^ (0x04C11DB7 if value & 0x80000000 else 0)) & 0xFFFFFFFF
    return value


def normalize(path: pathlib.Path) -> None:
    data = bytearray(path.read_bytes())
    serials = {}
    pos = 0
    while pos < len(data):
        if pos + 27 > len(data) or data[pos : pos + 4] != b"OggS":
            raise ValueError(f"{path}: invalid Ogg page at byte {pos}")
        segments = data[pos + 26]
        header = pos + 27 + segments
        if header > len(data):
            raise ValueError(f"{path}: truncated Ogg segment table")
        end = header + sum(data[pos + 27 : header])
        if end > len(data):
            raise ValueError(f"{path}: truncated Ogg page")
        old = bytes(data[pos + 14 : pos + 18])
        stable = serials.setdefault(old, len(serials) + 1)
        data[pos + 14 : pos + 18] = stable.to_bytes(4, "little")
        data[pos + 22 : pos + 26] = b"\0\0\0\0"
        data[pos + 22 : pos + 26] = crc(data[pos:end]).to_bytes(4, "little")
        pos = end
    path.write_bytes(data)


for name in sys.argv[1:]:
    normalize(pathlib.Path(name))
