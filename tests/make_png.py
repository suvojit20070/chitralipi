"""Writes a small test PNG without any library. Usage: python make_png.py out.png
SPDX-License-Identifier: MIT
"""
import struct
import sys
import zlib

w, h = 96, 64
raw = b"".join(b"\0" + bytes(v for x in range(w) for v in (x * 2, y * 4, (x + y) & 255)) for y in range(h))


def chunk(tag, data):
    return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data))


png = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) +
       chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))
with open(sys.argv[1], "wb") as f:
    f.write(png)
