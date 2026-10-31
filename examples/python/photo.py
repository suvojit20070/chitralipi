"""Photo -> string -> photo.
   python photo.py IMG.jpg 4000
SPDX-License-Identifier: MIT
"""
import sys

from PIL import Image

import chitralipi

src = sys.argv[1]
limit = int(sys.argv[2]) if len(sys.argv) > 2 else 4000

text = chitralipi.encode_image(Image.open(src), max_chars=limit)
with open("photo.txt", "w", encoding="utf-8") as f:
    f.write(text)

info = chitralipi.info(text)
print(f"{chitralipi.char_count(text)} characters, stored at {info['width']}x{info['height']}")

chitralipi.decode_image(text).save("back.png")
print("saved photo.txt and back.png")
