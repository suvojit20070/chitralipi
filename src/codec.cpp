// Planes, quantization, entropy coding and the binary container.
// SPDX-License-Identifier: MIT
#include "bitio.hpp"
#include "internal.hpp"

#include <algorithm>
#include <cstring>

namespace chl {

namespace {

inline uint32_t zigzag(int32_t q) {
    return q >= 0 ? uint32_t(q) << 1 : uint32_t((-int64_t(q)) * 2 - 1);
}

inline int32_t unzigzag(uint32_t u) {
    return (u & 1) ? int32_t(-((int64_t(u) + 1) / 2)) : int32_t(u >> 1);
}

inline int32_t quantize(int32_t c, uint32_t step) {
    if (step == 1) return c;
    int64_t a = c < 0 ? -int64_t(c) : int64_t(c);
    int32_t q = int32_t(a / step);
    return c < 0 ? -q : q;
}

inline int32_t dequantize(int32_t q, uint32_t step) {
    if (q == 0 || step == 1) return q;
    int64_t a = q < 0 ? -int64_t(q) : int64_t(q);
    int64_t c = ((2 * a + 1) * int64_t(step)) >> 1;
    if (c > INT32_MAX) c = INT32_MAX;
    return q < 0 ? int32_t(-c) : int32_t(c);
}

struct Adaptive {
    uint64_t a, n;
    int k;
    explicit Adaptive(int k0) : a(uint64_t(1) << k0), n(1), k(k0) {}
    void update(uint32_t m) {
        a += m;
        n += 1;
        if (n >= 64) {
            a = (a + 1) >> 1;
            n = 32;
        }
        k = 0;
        while (k < 24 && (n << k) < a) ++k;
    }
};

void encode_band(BitWriter& bw, const std::vector<int32_t>& q) {
    const size_t n = q.size();
    uint64_t sum = 0, count = 0;
    for (int32_t v : q) {
        if (v != 0) {
            sum += zigzag(v) - 1;
            ++count;
        }
    }
    int k0 = 0;
    if (count) {
        while (k0 < 15 && (count << k0) < sum) ++k0;
    }
    bw.put(uint32_t(k0), 4);
    Adaptive ad(k0);
    size_t pos = 0;
    for (;;) {
        size_t run = 0;
        while (pos + run < n && q[pos + run] == 0) ++run;
        bw.eg0(run);
        pos += run;
        if (pos == n) break;
        uint32_t m = zigzag(q[pos]) - 1;
        bw.rice(m, ad.k);
        ad.update(m);
        if (++pos == n) break;
    }
}

void decode_band(BitReader& br, Plane& p, const Band& b, uint32_t step) {
    const uint64_t n = uint64_t(b.w) * b.h;
    int k0 = int(br.get(4));
    Adaptive ad(k0);
    uint64_t pos = 0;
    for (;;) {
        uint64_t run = br.eg0();
        if (run > n - pos) fail(CHL_ERR_FORMAT, "run past end of band");
        pos += run;
        if (pos == n) break;
        uint32_t m = br.rice(ad.k);
        if (m == UINT32_MAX) fail(CHL_ERR_FORMAT, "coefficient too large");
        ad.update(m);
        int32_t q = unzigzag(m + 1);
        uint32_t x = b.x0 + uint32_t(pos % b.w), y = b.y0 + uint32_t(pos / b.w);
        p.at(x, y) = dequantize(q, step);
        if (++pos == n) break;
    }
}

uint32_t plane_base(int role, uint32_t base, uint8_t flags) {
    if (role == 1) return base == 1 ? 1u : base * 2;
    if (role == 2 && (flags & F_ALPHA_LOSSLESS)) return 1u;
    return base;
}

}  // namespace

int output_channels(uint8_t flags) {
    bool gray = flags & F_GRAY, alpha = flags & F_ALPHA;
    return gray ? (alpha ? 2 : 1) : (alpha ? 4 : 3);
}

Prepared prepare(const Image& img, int levels, bool chroma420, bool alpha_lossless) {
    const size_t count = size_t(img.w) * img.h;
    const int c = img.channels;
    const uint8_t* px = img.px;

    bool gray = c <= 2;
    if (!gray) {
        gray = true;
        for (size_t i = 0; i < count && gray; ++i) {
            const uint8_t* s = px + i * size_t(c);
            gray = s[0] == s[1] && s[1] == s[2];
        }
    }
    bool alpha = (c == 2 || c == 4);
    if (alpha) {
        bool opaque = true;
        for (size_t i = 0; i < count && opaque; ++i) opaque = px[i * size_t(c) + size_t(c) - 1] == 255;
        alpha = !opaque;
    }

    Prepared pr;
    pr.w = img.w;
    pr.h = img.h;
    pr.levels = levels;
    pr.flags = uint8_t((alpha ? F_ALPHA : 0) | (gray ? F_GRAY : 0) |
                       (alpha && alpha_lossless ? F_ALPHA_LOSSLESS : 0) |
                       (!gray && chroma420 ? F_CHROMA420 : 0));

    if (gray) {
        Plane y(img.w, img.h);
        for (size_t i = 0; i < count; ++i) y.v[i] = px[i * size_t(c)];
        pr.planes.push_back(std::move(y));
        pr.plane_role.push_back(0);
    } else {
        Plane y(img.w, img.h), co(img.w, img.h), cg(img.w, img.h);
        for (size_t i = 0; i < count; ++i) {
            const uint8_t* s = px + i * size_t(c);
            ycocg_forward(s[0], s[1], s[2], y.v[i], co.v[i], cg.v[i]);
        }
        if (pr.flags & F_CHROMA420) {
            co = chroma_down(co);
            cg = chroma_down(cg);
        }
        pr.planes.push_back(std::move(y));
        pr.planes.push_back(std::move(co));
        pr.planes.push_back(std::move(cg));
        pr.plane_role.insert(pr.plane_role.end(), {0, 1, 1});
    }
    if (alpha) {
        Plane a(img.w, img.h);
        for (size_t i = 0; i < count; ++i) a.v[i] = px[i * size_t(c) + size_t(c) - 1];
        pr.planes.push_back(std::move(a));
        pr.plane_role.push_back(2);
    }
    for (Plane& p : pr.planes) dwt_forward(p, levels);
    return pr;
}

Bytes emit(const Prepared& pr, uint32_t base_step) {
    BitWriter bw;
    std::vector<int32_t> q;
    for (size_t pi = 0; pi < pr.planes.size(); ++pi) {
        const Plane& p = pr.planes[pi];
        uint32_t base = plane_base(pr.plane_role[pi], base_step, pr.flags);
        for (const Band& b : band_layout(p.w, p.h, pr.levels)) {
            uint32_t step = band_step(base, b, pr.levels);
            q.resize(size_t(b.w) * b.h);
            size_t k = 0;
            for (uint32_t y = 0; y < b.h; ++y)
                for (uint32_t x = 0; x < b.w; ++x) q[k++] = quantize(p.at(b.x0 + x, b.y0 + y), step);
            encode_band(bw, q);
        }
    }
    Bytes data = bw.finish();

    Bytes out;
    out.reserve(data.size() + 32);
    out.push_back(kMagic0);
    out.push_back(kMagic1);
    out.push_back(uint8_t((kVersion << 4) | kModeCodec));
    put_varint(out, pr.w);
    put_varint(out, pr.h);
    out.push_back(pr.flags);
    out.push_back(uint8_t(pr.levels));
    put_varint(out, base_step);
    put_varint(out, data.size());
    out.insert(out.end(), data.begin(), data.end());
    uint32_t crc = crc32(out.data(), out.size());
    for (int s = 24; s >= 0; s -= 8) out.push_back(uint8_t(crc >> s));
    return out;
}

Bytes build_store(const uint8_t* data, size_t len, uint32_t kind) {
    if (len > kMaxStoreBytes) fail(CHL_ERR_LIMIT, "file too large");
    Bytes out;
    out.reserve(len + 20);
    out.push_back(kMagic0);
    out.push_back(kMagic1);
    out.push_back(uint8_t((kVersion << 4) | kModeStore));
    out.push_back(uint8_t(kind));
    put_varint(out, len);
    if (len) out.insert(out.end(), data, data + len);
    uint32_t crc = crc32(out.data(), out.size());
    for (int s = 24; s >= 0; s -= 8) out.push_back(uint8_t(crc >> s));
    return out;
}

Header parse_header(const uint8_t* d, size_t len) {
    if (len < 3 || d[0] != kMagic0 || d[1] != kMagic1) fail(CHL_ERR_FORMAT, "not a Chitralipi package");
    Header h;
    h.version = uint8_t(d[2] >> 4);
    h.mode = uint8_t(d[2] & 0x0F);
    if (h.version != kVersion) fail(CHL_ERR_UNSUPPORTED, "unsupported version");
    size_t pos = 3;
    if (h.mode == kModeCodec) {
        uint64_t w = get_varint(d, len, pos);
        uint64_t hh = get_varint(d, len, pos);
        if (w == 0 || hh == 0 || w > UINT32_MAX || hh > UINT32_MAX) fail(CHL_ERR_FORMAT, "bad image size");
        if (w * hh > kMaxPixels) fail(CHL_ERR_LIMIT, "image larger than the safety limit");
        if (pos + 2 > len) fail(CHL_ERR_FORMAT, "truncated header");
        h.w = uint32_t(w);
        h.h = uint32_t(hh);
        h.flags = d[pos++];
        if (h.flags & F_NOT_V1) fail(CHL_ERR_UNSUPPORTED, "unknown flags");
        if ((h.flags & F_GRAY) && (h.flags & F_CHROMA420)) fail(CHL_ERR_FORMAT, "gray image with chroma flag");
        if ((h.flags & F_ALPHA_LOSSLESS) && !(h.flags & F_ALPHA)) fail(CHL_ERR_FORMAT, "alpha flag mismatch");
        h.levels = d[pos++];
        if (h.levels > kMaxLevels) fail(CHL_ERR_FORMAT, "too many levels");
        uint64_t step = get_varint(d, len, pos);
        if (step < 1 || step > kMaxBaseStep) fail(CHL_ERR_FORMAT, "bad base step");
        h.base_step = uint32_t(step);
    } else if (h.mode == kModeStore) {
        if (pos >= len) fail(CHL_ERR_FORMAT, "truncated header");
        h.file_kind = d[pos++];
        if (h.file_kind > 6) fail(CHL_ERR_UNSUPPORTED, "unknown file kind");
    } else {
        fail(CHL_ERR_UNSUPPORTED, "unknown mode");
    }
    h.data_len = get_varint(d, len, pos);
    if (h.mode == kModeStore && h.data_len > kMaxStoreBytes) fail(CHL_ERR_LIMIT, "file too large");
    if (pos > len || len - pos < 4 || h.data_len > len - pos - 4) fail(CHL_ERR_FORMAT, "package is truncated");
    h.data_off = pos;
    h.total = pos + size_t(h.data_len) + 4;
    size_t crc_at = pos + size_t(h.data_len);
    uint32_t want = (uint32_t(d[crc_at]) << 24) | (uint32_t(d[crc_at + 1]) << 16) |
                    (uint32_t(d[crc_at + 2]) << 8) | uint32_t(d[crc_at + 3]);
    if (crc32(d, crc_at) != want) fail(CHL_ERR_CHECKSUM, "checksum mismatch");
    return h;
}

Decoded decode_codec(const Header& hd, const uint8_t* d) {
    const uint8_t flags = hd.flags;
    const bool gray = flags & F_GRAY, alpha = flags & F_ALPHA, c420 = flags & F_CHROMA420;
    const uint32_t cw = c420 ? (hd.w + 1) / 2 : hd.w, ch = c420 ? (hd.h + 1) / 2 : hd.h;

    std::vector<Plane> planes;
    std::vector<int> roles;
    planes.emplace_back(hd.w, hd.h);
    roles.push_back(0);
    if (!gray) {
        planes.emplace_back(cw, ch);
        planes.emplace_back(cw, ch);
        roles.insert(roles.end(), {1, 1});
    }
    if (alpha) {
        planes.emplace_back(hd.w, hd.h);
        roles.push_back(2);
    }

    BitReader br(d + hd.data_off, size_t(hd.data_len));
    for (size_t pi = 0; pi < planes.size(); ++pi) {
        Plane& p = planes[pi];
        uint32_t base = plane_base(roles[pi], hd.base_step, flags);
        for (const Band& b : band_layout(p.w, p.h, hd.levels)) decode_band(br, p, b, band_step(base, b, hd.levels));
    }
    if (!br.clean_end()) fail(CHL_ERR_FORMAT, "extra data after image");
    for (Plane& p : planes) dwt_inverse(p, hd.levels);

    Decoded out;
    out.w = hd.w;
    out.h = hd.h;
    out.channels = output_channels(flags);
    const size_t count = size_t(hd.w) * hd.h, oc = size_t(out.channels);
    out.px.resize(count * oc);
    auto clamp8 = [](int32_t v) -> uint8_t { return uint8_t(v < 0 ? 0 : (v > 255 ? 255 : v)); };

    if (gray) {
        for (size_t i = 0; i < count; ++i) out.px[i * oc] = clamp8(planes[0].v[i]);
    } else {
        if (c420) {
            planes[1] = chroma_up(planes[1], hd.w, hd.h);
            planes[2] = chroma_up(planes[2], hd.w, hd.h);
        }
        for (size_t i = 0; i < count; ++i) {
            int32_t r, g, b;
            ycocg_inverse(planes[0].v[i], planes[1].v[i], planes[2].v[i], r, g, b);
            out.px[i * oc] = clamp8(r);
            out.px[i * oc + 1] = clamp8(g);
            out.px[i * oc + 2] = clamp8(b);
        }
    }
    if (alpha) {
        const Plane& a = planes.back();
        for (size_t i = 0; i < count; ++i) out.px[i * oc + oc - 1] = clamp8(a.v[i]);
    }
    return out;
}

}  // namespace chl
