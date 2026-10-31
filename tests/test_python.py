"""Python binding test. Run: CHITRALIPI_LIB=build/libchitralipi.so python tests/test_python.py
SPDX-License-Identifier: MIT
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "bindings", "python"))
import chitralipi  # noqa: E402

w, h = 40, 30
px = bytes((x * 6 + y * 3 + k * 50) & 255 for y in range(h) for x in range(w) for k in range(3))

text = chitralipi.encode(px, w, h, 3, quality=100)
assert text.startswith("CHL1:")
assert chitralipi.decode(text) == (px, w, h, 3), "lossless round trip"

small = chitralipi.encode(px, w, h, 3, max_chars=250)
assert chitralipi.char_count(small) <= 250
assert len(small) == chitralipi.char_count(small)

ascii_text = chitralipi.encode(px, w, h, 3, quality=100, ascii=True)
assert ascii_text.startswith("CHL1a:") and ascii_text.isascii()
assert chitralipi.decode(ascii_text)[0] == px

blob = bytes(range(256)) * 3
assert chitralipi.unpack(chitralipi.pack(blob)) == blob

try:
    chitralipi.decode("CHL1:x")
    raise AssertionError("broken text must raise")
except chitralipi.ChitralipiError as e:
    assert e.status == 4

info = chitralipi.info(text)
assert info["width"] == w and info["base_step"] == 1
print(f"Python {chitralipi.version()}: all ok ({chitralipi.char_count(text)} chars lossless, "
      f"{chitralipi.char_count(small)} chars small)")
