# Chitralipi Format, Version 1

Status: stable for v1. A decoder that follows this document must produce exactly the same pixels as the reference implementation.

Notation: `>>` is arithmetic right shift (floor division by a power of two, also for negative numbers). All values are integers. Bytes and bits are big-endian / most significant bit first unless a field says otherwise.

## 1. Layers

```text
pixels --codec--> binary package --text envelope--> "CHL1:..." string
```

A binary package can also be stored as a `.chl` file (MIME `application/x-chitralipi`).

## 2. Text envelope

| Prefix | Encoding | Bits per character |
| --- | --- | --- |
| `CHL1:` | Lipi-14 | 14 |
| `CHL1a:` | Base64URL without padding (`A-Z a-z 0-9 - _`) | 6 |

**Lipi-14.** Read the package as one bit stream. Cut it into 14-bit groups; zero-fill the last group on the right. Each group value `v` (0..16383) becomes the code point `U+4E00 + v` (range U+4E00..U+8DFF, CJK Unified Ideographs). These code points are in the BMP (one UTF-16 unit each) and are unchanged by NFC, NFD, NFKC and NFKD.

**Decoding text.** Skip an optional UTF-8 BOM and leading whitespace, match a prefix, then skip ASCII space, tab, CR and LF anywhere. Any other character outside the alphabet is an error. Leftover bits after the last full byte must be zero. Lipi-14 can produce one extra zero byte at the end; a decoder ignores exactly one trailing zero byte after a complete package when the text was Lipi-14. Base64URL text with a symbol count of 4k+1 is an error.

## 3. Binary package

All packages start with:

| Field | Size | Value |
| --- | --- | --- |
| Magic | 2 bytes | `0x43 0x4C` ("CL") |
| VerMode | 1 byte | high nibble = version (1), low nibble = mode (0 packed file, 1 image codec) |

Integers marked *varint* are unsigned LEB128: 7 bits per byte, least significant group first, high bit set means another byte follows, at most 10 bytes.

### Mode 1: image codec

| Field | Size |
| --- | --- |
| Width | varint, ≥ 1 |
| Height | varint, ≥ 1 |
| Flags | 1 byte |
| Levels | 1 byte, 0..15 |
| BaseStep | varint, 1..1048576 |
| DataLen | varint |
| Data | DataLen bytes |
| CRC32 | 4 bytes, big-endian |

Flags:

| Bit | Meaning |
| --- | --- |
| 0 | alpha plane present |
| 1 | chroma 4:2:0 (never with bit 2) |
| 2 | grayscale (luma plane only) |
| 3 | alpha lossless (requires bit 0) |
| 4-5 | entropy mode, must be 0 in v1 |
| 6-7 | reserved, must be 0 |

### Mode 0: packed file

| Field | Size |
| --- | --- |
| Kind | 1 byte: 0 unknown, 1 JPEG, 2 PNG, 3 WebP, 4 AVIF, 5 GIF, 6 HEIF |
| DataLen | varint |
| Data | the original file bytes |
| CRC32 | 4 bytes, big-endian |

CRC32 is the standard CRC-32 (IEEE 802.3, polynomial 0xEDB88320 reflected, as in zlib) over every byte from Magic to the end of Data.

## 4. Planes

Output channels: grayscale 1 (2 with alpha), color 3 (4 with alpha).

Planes in order: Y; then Co, Cg unless grayscale; then A if alpha. With chroma 4:2:0, Co and Cg are `ceil(W/2) × ceil(H/2)`; every other plane is `W × H`.

**Color transform (YCoCg-R).**

```text
forward: Co = R - B;  t = B + (Co >> 1);  Cg = G - t;  Y = t + (Cg >> 1)
inverse: t = Y - (Cg >> 1);  G = Cg + t;  B = t - (Co >> 1);  R = B + Co
```

Grayscale planes hold the gray value directly; the decoder outputs it as R = G = B. After decoding, every channel is clamped to 0..255.

**Chroma 4:2:0.** Encoder: each output sample is `(a + b + c + d + 2) >> 2` over its 2×2 block, repeating the last row/column at an odd edge. Decoder: each chroma sample is copied to its 2×2 block.

## 5. Wavelet (LeGall 5/3, integer lifting)

One dimension, `n` samples (nothing happens when `n < 2`):

```text
mirror(j) = -j            if j < 0
            2(n - 1) - j  if j >= n
            j             otherwise

step 1, every odd i:   x[i] -= (x[mirror(i-1)] + x[mirror(i+1)]) >> 1
step 2, every even i:  x[i] += (x[mirror(i-1)] + x[mirror(i+1)] + 2) >> 2
then store even positions first (ceil(n/2) values), then odd positions (floor(n/2) values)
```

The inverse interleaves, undoes step 2 on even positions, then step 1 on odd positions.

Two dimensions: at each level, transform every row of the current `w × h` low-low region, then every column. The next region is `ceil(w/2) × ceil(h/2)` at the top-left. Repeat `Levels` times. The decoder runs the levels in reverse, columns before rows.

## 6. Bands and quantization

With `w(l)`, `h(l)` the region size after `l` levels (`w(0) = W`), the bands are:

| Band | Position | Size |
| --- | --- | --- |
| LL | (0, 0) | `w(L) × h(L)` |
| HL, level l | (`w(l)`, 0) | `(w(l-1) - w(l)) × h(l)` |
| LH, level l | (0, `h(l)`) | `w(l) × (h(l-1) - h(l))` |
| HH, level l | (`w(l)`, `h(l)`) | `(w(l-1) - w(l)) × (h(l-1) - h(l))` |

Bands with zero samples are skipped completely.

Plane base `B`: Y uses BaseStep; Co/Cg use 1 if BaseStep = 1, else 2 × BaseStep; A uses 1 when "alpha lossless" is set, else BaseStep.

```text
LL step              = max(1, B >> Levels)
HL/LH step, level l  = max(1, (B × 16) >> (3 + l))
HH step, level l     = max(1, (B × 22) >> (3 + l))

quantize:   q = sign(c) × floor(|c| / step)
dequantize: c = 0 if q = 0, else sign(q) × (((2|q| + 1) × step) >> 1)
```

With BaseStep = 1 every step is 1 and the image is pixel-lossless.

## 7. Entropy coding

Data is one bit stream, most significant bit first, zero-padded to a byte. Order: planes as in section 4; inside a plane LL first, then for l = L down to 1: HL, LH, HH; inside a band row by row, left to right.

Each non-empty band:

```text
k0 = read 4 bits;  N = 1;  A = 1 << k0;  k = k0;  pos = 0
loop:
    run = EG0();  pos += run;  if pos == size: done      (run past size = error)
    m = RICE(k);  u = m + 1
    q = u even ? u / 2 : -(u + 1) / 2
    A += m;  N += 1;  if N >= 64: A = (A + 1) >> 1, N = 32
    k = smallest value in 0..24 with (N << k) >= A  (24 if none)
    pos += 1;  if pos == size: done
```

- **EG0** (Exp-Golomb order 0): count zero bits `z` up to the first one bit (more than 62 is an error), read `z` more bits `r`, result `(1 << z | r) - 1`.
- **RICE(k)**: count one bits up to 24 or the first zero bit. If 24 ones: read 32 bits, that is `m`. Otherwise `m = (ones << k) | read k bits`.

The encoder maps `q` to `u = 2q` (q ≥ 0) or `-2q - 1` (q < 0) and writes `m = u - 1` for every non-zero value. Its choice of `k0` is free; the reference uses the smallest `k0 ≤ 15` with `count << k0 ≥ sum of m`.

After the last band the reader must have used every Data byte and every padding bit must be zero.

## 8. Decoder safety rules

Reject: unknown version, mode, flags or kind; Width × Height above 2^26; Levels above 15; BaseStep outside 1..2^20; DataLen past the end; CRC mismatch; bytes after the package (except the one Lipi-14 zero byte); runs past the end of a band; a bit stream that ends early or has leftover data.

## 9. Worked example

2×2 RGB image, Levels 1, BaseStep 1: red (255,0,0), green (0,255,0) in row 0; blue (0,0,255), white (255,255,255) in row 1.

| Plane | Row 0 | Row 1 | LL | HL | LH | HH |
| --- | --- | --- | --- | --- | --- | --- |
| Y | 63, 127 | 63, 255 | 127 | 128 | 64 | 128 |
| Co | 255, 0 | -255, 0 | 1 | 0 | -255 | 510 |
| Cg | -127, 255 | -127, 0 | 1 | 255 | -127 | -255 |

Red: `Co = 255`, `t = 127`, `Cg = -127`, `Y = 127 + (-127 >> 1) = 63`.

The Y LL value 127 with `k0 = 7` is written as `0111` (k0), `1` (EG0 run 0), `10` (Rice quotient 1), `1111101` (low 7 bits of m = 253).

The magic bytes `0x43 0x4C` in Lipi-14: bits `01000011010011` = 4307 → U+5ED3, then `00` + zero fill → U+4E00.

These values are checked by `tests/test_main.cpp`.
