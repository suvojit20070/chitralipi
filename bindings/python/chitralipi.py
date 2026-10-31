"""Chitralipi for Python - image to text string and back.

Needs the shared library (libchitralipi.so / chitralipi.dll / libchitralipi.dylib).
It is searched next to this file, then in CHITRALIPI_LIB (a full path), then
on the system library path.

    import chitralipi
    text = chitralipi.encode(rgb_bytes, width, height, 3, quality=80)
    pixels, width, height, channels = chitralipi.decode(text)

With Pillow installed:

    text = chitralipi.encode_image(PIL.Image.open("photo.jpg"), max_chars=4000)
    chitralipi.decode_image(text).save("back.png")

SPDX-License-Identifier: MIT
"""

import ctypes
import ctypes.util
import os
import sys

__all__ = ["ChitralipiError", "encode", "decode", "pack", "unpack", "info",
           "encode_image", "decode_image", "char_count", "version"]


class ChitralipiError(Exception):
    def __init__(self, status, message):
        super().__init__(message)
        self.status = status


class _Options(ctypes.Structure):
    _fields_ = [("quality", ctypes.c_int32), ("base_step", ctypes.c_int32),
                ("levels", ctypes.c_int32), ("chroma_420", ctypes.c_int32),
                ("alpha_lossless", ctypes.c_int32), ("text_mode", ctypes.c_int32),
                ("allow_resize", ctypes.c_int32), ("reserved", ctypes.c_int32),
                ("max_chars", ctypes.c_uint64)]


class _Info(ctypes.Structure):
    _fields_ = [("version", ctypes.c_int32), ("mode", ctypes.c_int32),
                ("channels", ctypes.c_int32), ("levels", ctypes.c_int32),
                ("width", ctypes.c_uint32), ("height", ctypes.c_uint32),
                ("base_step", ctypes.c_uint32), ("file_kind", ctypes.c_uint32),
                ("flags", ctypes.c_uint32), ("reserved", ctypes.c_uint32),
                ("data_bytes", ctypes.c_uint64), ("package_bytes", ctypes.c_uint64),
                ("chars", ctypes.c_uint64)]


def _load():
    names = {"win32": ["chitralipi.dll", "libchitralipi.dll"],
             "darwin": ["libchitralipi.dylib", "libchitralipi.1.dylib"]}.get(
        sys.platform, ["libchitralipi.so", "libchitralipi.so.1"])
    here = os.path.dirname(os.path.abspath(__file__))
    candidates = [os.path.join(here, n) for n in names]
    if os.environ.get("CHITRALIPI_LIB"):
        candidates.insert(0, os.environ["CHITRALIPI_LIB"])
    found = ctypes.util.find_library("chitralipi")
    if found:
        candidates.append(found)
    for path in candidates:
        if os.path.exists(path) or path == found:
            try:
                return ctypes.CDLL(path)
            except OSError:
                continue
    raise OSError("Chitralipi library not found. Put it next to chitralipi.py or set CHITRALIPI_LIB.")


_lib = _load()
_c = ctypes
_u8p = _c.POINTER(_c.c_uint8)
_lib.chl_version.restype = _c.c_char_p
_lib.chl_status_string.restype = _c.c_char_p
_lib.chl_status_string.argtypes = [_c.c_int32]
_lib.chl_options_default.argtypes = [_c.POINTER(_Options)]
_lib.chl_free.argtypes = [_c.c_void_p]
_lib.chl_char_count.restype = _c.c_uint64
_lib.chl_char_count.argtypes = [_c.c_char_p, _c.c_size_t]
_lib.chl_encode.argtypes = [_c.c_char_p, _c.c_uint32, _c.c_uint32, _c.c_int32, _c.POINTER(_Options),
                            _c.POINTER(_c.c_void_p), _c.POINTER(_c.c_size_t)]
_lib.chl_decode.argtypes = [_c.c_char_p, _c.c_size_t, _c.POINTER(_c.c_void_p), _c.POINTER(_c.c_uint32),
                            _c.POINTER(_c.c_uint32), _c.POINTER(_c.c_int32)]
_lib.chl_pack.argtypes = [_c.c_char_p, _c.c_size_t, _c.c_int32, _c.POINTER(_c.c_void_p),
                          _c.POINTER(_c.c_size_t)]
_lib.chl_unpack.argtypes = [_c.c_char_p, _c.c_size_t, _c.POINTER(_c.c_void_p), _c.POINTER(_c.c_size_t),
                            _c.POINTER(_c.c_uint32)]
_lib.chl_get_info.argtypes = [_c.c_char_p, _c.c_size_t, _c.POINTER(_Info)]
for _f in ("chl_encode", "chl_decode", "chl_pack", "chl_unpack", "chl_get_info"):
    getattr(_lib, _f).restype = _c.c_int32


def _check(status):
    if status != 0:
        raise ChitralipiError(status, _lib.chl_status_string(status).decode())


def _take_bytes(ptr, n):
    try:
        return _c.string_at(ptr.value, n) if n else b""
    finally:
        _lib.chl_free(ptr)


def version():
    return _lib.chl_version().decode()


def char_count(text):
    raw = text.encode("utf-8")
    return _lib.chl_char_count(raw, len(raw))


def encode(pixels, width, height, channels, quality=80, max_chars=0, ascii=False,
           allow_resize=True, levels=5, step=0):
    """Pixels (bytes, row-major, 1-4 channels) -> Chitralipi string."""
    data = bytes(pixels)
    if len(data) != width * height * channels:
        raise ValueError("pixel buffer size does not match width * height * channels")
    opt = _Options()
    _lib.chl_options_default(_c.byref(opt))
    opt.quality, opt.max_chars, opt.text_mode = quality, max_chars, 1 if ascii else 0
    opt.allow_resize, opt.levels, opt.base_step = int(allow_resize), levels, step
    out, n = _c.c_void_p(), _c.c_size_t()
    _check(_lib.chl_encode(data, width, height, channels, _c.byref(opt), _c.byref(out), _c.byref(n)))
    return _take_bytes(out, n.value).decode("utf-8")


def decode(text):
    """Chitralipi string -> (pixels: bytes, width, height, channels)."""
    raw = text.encode("utf-8")
    out, w, h, ch = _c.c_void_p(), _c.c_uint32(), _c.c_uint32(), _c.c_int32()
    _check(_lib.chl_decode(raw, len(raw), _c.byref(out), _c.byref(w), _c.byref(h), _c.byref(ch)))
    return _take_bytes(out, w.value * h.value * ch.value), w.value, h.value, ch.value


def pack(file_bytes, ascii=False):
    """Any file -> string, byte-for-byte exact."""
    data = bytes(file_bytes)
    out, n = _c.c_void_p(), _c.c_size_t()
    _check(_lib.chl_pack(data, len(data), 1 if ascii else 0, _c.byref(out), _c.byref(n)))
    return _take_bytes(out, n.value).decode("utf-8")


def unpack(text):
    """String made by pack() -> original file bytes."""
    raw = text.encode("utf-8")
    out, n, kind = _c.c_void_p(), _c.c_size_t(), _c.c_uint32()
    _check(_lib.chl_unpack(raw, len(raw), _c.byref(out), _c.byref(n), _c.byref(kind)))
    return _take_bytes(out, n.value)


def info(text):
    raw = text.encode("utf-8")
    i = _Info()
    _check(_lib.chl_get_info(raw, len(raw), _c.byref(i)))
    return {name: getattr(i, name) for name, _ in _Info._fields_ if name != "reserved"}


_MODES = {1: "L", 2: "LA", 3: "RGB", 4: "RGBA"}


def encode_image(image, **kwargs):
    """PIL.Image -> string. Keyword arguments are the same as encode()."""
    if image.mode not in ("L", "LA", "RGB", "RGBA"):
        image = image.convert("RGBA" if "A" in image.getbands() else "RGB")
    channels = len(image.mode)
    return encode(image.tobytes(), image.width, image.height, channels, **kwargs)


def decode_image(text):
    """String -> PIL.Image."""
    from PIL import Image
    pixels, w, h, ch = decode(text)
    return Image.frombytes(_MODES[ch], (w, h), pixels)
