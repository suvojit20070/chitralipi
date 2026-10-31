// Chroma resampling and the LeGall 5/3 integer wavelet.
// SPDX-License-Identifier: MIT
#include "internal.hpp"

#include <algorithm>

namespace chl {

Plane chroma_down(const Plane& p) {
    Plane o((p.w + 1) / 2, (p.h + 1) / 2);
    for (uint32_t y = 0; y < o.h; ++y) {
        uint32_t y0 = 2 * y, y1 = std::min(2 * y + 1, p.h - 1);
        for (uint32_t x = 0; x < o.w; ++x) {
            uint32_t x0 = 2 * x, x1 = std::min(2 * x + 1, p.w - 1);
            int32_t s = p.at(x0, y0) + p.at(x1, y0) + p.at(x0, y1) + p.at(x1, y1);
            o.at(x, y) = (s + 2) >> 2;
        }
    }
    return o;
}

Plane chroma_up(const Plane& p, uint32_t w, uint32_t h) {
    Plane o(w, h);
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x) o.at(x, y) = p.at(x / 2, y / 2);
    return o;
}

namespace {

inline size_t mirror(ptrdiff_t j, size_t n) {
    if (j < 0) j = -j;
    if (j >= ptrdiff_t(n)) j = 2 * (ptrdiff_t(n) - 1) - j;
    return size_t(j);
}

void fwd1d(int32_t* x, size_t n, size_t stride, std::vector<int32_t>& t) {
    if (n < 2) return;
    t.resize(n);
    for (size_t i = 0; i < n; ++i) t[i] = x[i * stride];
    for (size_t i = 1; i < n; i += 2)
        t[i] -= (t[mirror(ptrdiff_t(i) - 1, n)] + t[mirror(ptrdiff_t(i) + 1, n)]) >> 1;
    for (size_t i = 0; i < n; i += 2)
        t[i] += (t[mirror(ptrdiff_t(i) - 1, n)] + t[mirror(ptrdiff_t(i) + 1, n)] + 2) >> 2;
    size_t nl = (n + 1) / 2;
    for (size_t i = 0; i < nl; ++i) x[i * stride] = t[2 * i];
    for (size_t i = 0; i < n / 2; ++i) x[(nl + i) * stride] = t[2 * i + 1];
}

void inv1d(int32_t* x, size_t n, size_t stride, std::vector<int32_t>& t) {
    if (n < 2) return;
    t.resize(n);
    size_t nl = (n + 1) / 2;
    for (size_t i = 0; i < nl; ++i) t[2 * i] = x[i * stride];
    for (size_t i = 0; i < n / 2; ++i) t[2 * i + 1] = x[(nl + i) * stride];
    for (size_t i = 0; i < n; i += 2)
        t[i] -= (t[mirror(ptrdiff_t(i) - 1, n)] + t[mirror(ptrdiff_t(i) + 1, n)] + 2) >> 2;
    for (size_t i = 1; i < n; i += 2)
        t[i] += (t[mirror(ptrdiff_t(i) - 1, n)] + t[mirror(ptrdiff_t(i) + 1, n)]) >> 1;
    for (size_t i = 0; i < n; ++i) x[i * stride] = t[i];
}

void level_sizes(uint32_t w, uint32_t h, int levels, std::vector<uint32_t>& ws, std::vector<uint32_t>& hs) {
    ws.assign(size_t(levels) + 1, 0);
    hs.assign(size_t(levels) + 1, 0);
    ws[0] = w;
    hs[0] = h;
    for (int l = 1; l <= levels; ++l) {
        ws[l] = (ws[l - 1] + 1) / 2;
        hs[l] = (hs[l - 1] + 1) / 2;
    }
}

}  // namespace

void dwt_forward(Plane& p, int levels) {
    std::vector<int32_t> t;
    uint32_t w = p.w, h = p.h;
    for (int l = 0; l < levels; ++l) {
        for (uint32_t y = 0; y < h; ++y) fwd1d(&p.v[size_t(y) * p.w], w, 1, t);
        for (uint32_t x = 0; x < w; ++x) fwd1d(&p.v[x], h, p.w, t);
        w = (w + 1) / 2;
        h = (h + 1) / 2;
    }
}

void dwt_inverse(Plane& p, int levels) {
    std::vector<int32_t> t;
    std::vector<uint32_t> ws, hs;
    level_sizes(p.w, p.h, levels, ws, hs);
    for (int l = levels - 1; l >= 0; --l) {
        uint32_t w = ws[size_t(l)], h = hs[size_t(l)];
        for (uint32_t x = 0; x < w; ++x) inv1d(&p.v[x], h, p.w, t);
        for (uint32_t y = 0; y < h; ++y) inv1d(&p.v[size_t(y) * p.w], w, 1, t);
    }
}

std::vector<Band> band_layout(uint32_t w, uint32_t h, int levels) {
    std::vector<uint32_t> ws, hs;
    level_sizes(w, h, levels, ws, hs);
    std::vector<Band> bands;
    auto add = [&](uint32_t x0, uint32_t y0, uint32_t bw, uint32_t bh, int level, BandKind kind) {
        if (bw > 0 && bh > 0) bands.push_back(Band{x0, y0, bw, bh, level, kind});
    };
    const size_t L = size_t(levels);
    add(0, 0, ws[L], hs[L], levels, BAND_LL);
    for (int l = levels; l >= 1; --l) {
        uint32_t wp = ws[size_t(l) - 1], hp = hs[size_t(l) - 1];
        uint32_t wl = ws[size_t(l)], hl = hs[size_t(l)];
        add(wl, 0, wp - wl, hl, l, BAND_HL);
        add(0, hl, wl, hp - hl, l, BAND_LH);
        add(wl, hl, wp - wl, hp - hl, l, BAND_HH);
    }
    return bands;
}

uint32_t band_step(uint32_t base, const Band& band, int levels) {
    uint64_t s;
    if (band.kind == BAND_LL) {
        s = uint64_t(base) >> levels;
    } else {
        uint64_t weight = band.kind == BAND_HH ? 22 : 16;
        s = (uint64_t(base) * weight) >> (3 + band.level);
    }
    return s < 1 ? 1u : uint32_t(s);
}

}  // namespace chl
