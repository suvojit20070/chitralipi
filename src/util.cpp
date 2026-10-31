// CRC-32, varints, area resize and file-type sniffing.
// SPDX-License-Identifier: MIT
#include "internal.hpp"

#include <cstring>

namespace chl {

uint32_t crc32(const uint8_t* data, size_t len) {
    static uint32_t table[256];
    static bool ready = [] {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        return true;
    }();
    (void)ready;
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i) c = table[(c ^ data[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

void put_varint(Bytes& out, uint64_t v) {
    while (v >= 0x80) {
        out.push_back(uint8_t(v | 0x80));
        v >>= 7;
    }
    out.push_back(uint8_t(v));
}

uint64_t get_varint(const uint8_t* d, size_t len, size_t& pos) {
    uint64_t v = 0;
    for (int i = 0; i < 10; ++i) {
        if (pos >= len) fail(CHL_ERR_FORMAT, "truncated number");
        uint8_t b = d[pos++];
        if (i == 9 && b > 1) fail(CHL_ERR_FORMAT, "number too large");
        v |= uint64_t(b & 0x7F) << (7 * i);
        if (!(b & 0x80)) return v;
    }
    fail(CHL_ERR_FORMAT, "number too long");
}

Bytes resize_area(const uint8_t* px, uint32_t w, uint32_t h, int channels, uint32_t nw, uint32_t nh) {
    const size_t c = size_t(channels);
    // Horizontal pass.
    std::vector<float> mid(size_t(nw) * h * c, 0.f);
    const double sx = double(w) / nw;
    for (uint32_t x = 0; x < nw; ++x) {
        double a = x * sx, b = (x + 1) * sx;
        uint32_t i0 = uint32_t(a), i1 = uint32_t(b);
        if (i1 >= w) i1 = w - 1;
        for (uint32_t i = i0; i <= i1; ++i) {
            double lo = i > a ? i : a, hi = (i + 1) < b ? (i + 1) : b;
            float wt = float((hi - lo) / sx);
            if (wt <= 0) continue;
            for (uint32_t y = 0; y < h; ++y)
                for (size_t k = 0; k < c; ++k)
                    mid[(size_t(y) * nw + x) * c + k] += wt * px[(size_t(y) * w + i) * c + k];
        }
    }
    // Vertical pass.
    std::vector<float> acc(size_t(nw) * nh * c, 0.f);
    const double sy = double(h) / nh;
    for (uint32_t y = 0; y < nh; ++y) {
        double a = y * sy, b = (y + 1) * sy;
        uint32_t j0 = uint32_t(a), j1 = uint32_t(b);
        if (j1 >= h) j1 = h - 1;
        for (uint32_t j = j0; j <= j1; ++j) {
            double lo = j > a ? j : a, hi = (j + 1) < b ? (j + 1) : b;
            float wt = float((hi - lo) / sy);
            if (wt <= 0) continue;
            for (size_t i = 0; i < size_t(nw) * c; ++i) acc[size_t(y) * nw * c + i] += wt * mid[size_t(j) * nw * c + i];
        }
    }
    Bytes out(acc.size());
    for (size_t i = 0; i < acc.size(); ++i) {
        float v = acc[i] + 0.5f;
        out[i] = uint8_t(v < 0 ? 0 : (v > 255 ? 255 : v));
    }
    return out;
}

uint32_t detect_file_kind(const uint8_t* d, size_t n) {
    if (n >= 3 && d[0] == 0xFF && d[1] == 0xD8 && d[2] == 0xFF) return 1;
    if (n >= 8 && std::memcmp(d, "\x89PNG\r\n\x1a\n", 8) == 0) return 2;
    if (n >= 12 && std::memcmp(d, "RIFF", 4) == 0 && std::memcmp(d + 8, "WEBP", 4) == 0) return 3;
    if (n >= 6 && (std::memcmp(d, "GIF87a", 6) == 0 || std::memcmp(d, "GIF89a", 6) == 0)) return 5;
    if (n >= 12 && std::memcmp(d + 4, "ftyp", 4) == 0) {
        if (std::memcmp(d + 8, "avif", 4) == 0 || std::memcmp(d + 8, "avis", 4) == 0) return 4;
        if (std::memcmp(d + 8, "heic", 4) == 0 || std::memcmp(d + 8, "heix", 4) == 0 ||
            std::memcmp(d + 8, "mif1", 4) == 0 || std::memcmp(d + 8, "msf1", 4) == 0)
            return 6;
    }
    return 0;
}

}  // namespace chl
