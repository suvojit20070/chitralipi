// Chitralipi test suite. Exit code 0 = all passed.
// SPDX-License-Identifier: MIT
#include "chitralipi/chitralipi.h"
#include "internal.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

static int g_failed = 0, g_passed = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (cond) {                                                              \
            ++g_passed;                                                          \
        } else {                                                                 \
            ++g_failed;                                                          \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                        \
    } while (0)

namespace {

struct Img {
    std::vector<uint8_t> px;
    uint32_t w, h;
    int c;
};

Img make_image(uint32_t w, uint32_t h, int c, unsigned seed, bool noisy) {
    Img im{std::vector<uint8_t>(size_t(w) * h * size_t(c)), w, h, c};
    std::mt19937 rng(seed);
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x)
            for (int k = 0; k < c; ++k) {
                int v = int((x * 255) / (w > 1 ? w - 1 : 1) + (y * 97) / (h ? h : 1) + k * 40) & 255;
                if (noisy) v = (v + int(rng() % 64)) & 255;
                if (k == 3 || (c == 2 && k == 1)) v = int((x + y) % 7 == 0 ? 0 : 255 - (x * 3) % 128);
                im.px[(size_t(y) * w + x) * size_t(c) + size_t(k)] = uint8_t(v);
            }
    return im;
}

std::string encode(const Img& im, const chl_options& o, int32_t* status = nullptr) {
    char* t = nullptr;
    size_t n = 0;
    int32_t st = chl_encode(im.px.data(), im.w, im.h, im.c, &o, &t, &n);
    if (status) *status = st;
    if (st != CHL_OK) return "";
    std::string s(t, n);
    chl_free(t);
    return s;
}

bool decode(const std::string& s, Img& out, int32_t* status = nullptr) {
    uint8_t* px = nullptr;
    int32_t st = chl_decode(s.data(), s.size(), &px, &out.w, &out.h, &out.c);
    if (status) *status = st;
    if (st != CHL_OK) return false;
    out.px.assign(px, px + size_t(out.w) * out.h * size_t(out.c));
    chl_free(px);
    return true;
}

double psnr(const Img& a, const Img& b) {
    double se = 0;
    for (size_t i = 0; i < a.px.size(); ++i) {
        double d = double(a.px[i]) - b.px[i];
        se += d * d;
    }
    if (se == 0) return 99;
    return 10 * std::log10(255.0 * 255.0 / (se / double(a.px.size())));
}

chl_options defaults() {
    chl_options o;
    chl_options_default(&o);
    return o;
}

void test_spec_example() {
    // The 2x2 example from SPEC.md, section "Worked example".
    using namespace chl;
    int32_t y, co, cg;
    ycocg_forward(255, 0, 0, y, co, cg);
    CHECK(y == 63 && co == 255 && cg == -127);
    Plane p(2, 2);
    p.at(0, 0) = 63;
    p.at(1, 0) = 127;
    p.at(0, 1) = 63;
    p.at(1, 1) = 255;
    dwt_forward(p, 1);
    CHECK(p.at(0, 0) == 127 && p.at(1, 0) == 128 && p.at(0, 1) == 64 && p.at(1, 1) == 128);
    dwt_inverse(p, 1);
    CHECK(p.at(0, 0) == 63 && p.at(1, 0) == 127 && p.at(0, 1) == 63 && p.at(1, 1) == 255);

    Bytes sg = {'C', 'L'};
    std::string t = text_encode(sg.data(), sg.size(), CHL_TEXT_UNICODE);
    // 0x43 0x4C -> 01000011 01001100 -> 14 bits 01000011010011 = 4307 -> U+5ED3, then 00 + zeros -> U+4E00
    CHECK(t == std::string("CHL1:") + "\xE5\xBB\x93" + "\xE4\xB8\x80");
}

void test_wavelet_random() {
    std::mt19937 rng(7);
    for (int it = 0; it < 200; ++it) {
        uint32_t w = 1 + rng() % 40, h = 1 + rng() % 40;
        int levels = int(rng() % 7);
        chl::Plane p(w, h);
        for (auto& v : p.v) v = int32_t(rng() % 512) - 256;
        chl::Plane orig = p;
        chl::dwt_forward(p, levels);
        chl::dwt_inverse(p, levels);
        CHECK(p.v == orig.v);
    }
}

void test_lossless_roundtrip() {
    const uint32_t sizes[][2] = {{1, 1}, {1, 7}, {7, 1}, {2, 2}, {3, 5}, {16, 16}, {33, 17}, {257, 131}};
    for (int c = 1; c <= 4; ++c)
        for (auto& sz : sizes)
            for (int mode = 0; mode < 2; ++mode) {
                Img im = make_image(sz[0], sz[1], c, sz[0] * 31 + sz[1] + unsigned(c), true);
                chl_options o = defaults();
                o.quality = 100;
                o.text_mode = mode;
                std::string s = encode(im, o);
                Img out{};
                bool ok = decode(s, out);
                CHECK(ok);
                if (!ok) continue;
                // Gray/alpha may be detected; compare the channels that exist.
                bool same = out.w == im.w && out.h == im.h;
                for (size_t i = 0; same && i < size_t(im.w) * im.h; ++i) {
                    const uint8_t* a = &im.px[i * size_t(im.c)];
                    const uint8_t* b = &out.px[i * size_t(out.c)];
                    int ac = im.c, bc = out.c;
                    uint8_t ar = a[0], ag = ac >= 3 ? a[1] : a[0], ab = ac >= 3 ? a[2] : a[0];
                    uint8_t br = b[0], bg = bc >= 3 ? b[1] : b[0], bb = bc >= 3 ? b[2] : b[0];
                    uint8_t aa = (ac == 2 || ac == 4) ? a[ac - 1] : 255;
                    uint8_t ba = (bc == 2 || bc == 4) ? b[bc - 1] : 255;
                    same = ar == br && ag == bg && ab == bb && aa == ba;
                }
                CHECK(same);
            }
}

// Strings made by v1.0 (entropy mode 0, run + Rice) must still decode.
void test_both_entropy_modes() {
    for (int entropy : {chl::kEntropyRice, chl::kEntropyArith}) {
        for (int c : {1, 3, 4}) {
            Img im = make_image(77, 41, c, unsigned(9 + c), true);
            for (uint32_t step : {1u, 7u, 60u}) {
                auto prep = chl::prepare(chl::Image{im.px.data(), im.w, im.h, c}, 5, step > 1, true, entropy);
                chl::Bytes pkg = chl::emit(prep, step);
                CHECK(bool(pkg[5] & chl::F_ENTROPY_AC) == (entropy == chl::kEntropyArith));
                std::string s = chl::text_encode(pkg.data(), pkg.size(), CHL_TEXT_UNICODE);
                Img out{};
                CHECK(decode(s, out));
                if (step == 1 && c == 3) CHECK(out.px == im.px);
            }
        }
    }
    // The arithmetic coder must be smaller than Rice on a real-looking image.
    Img im = make_image(256, 256, 3, 77, true);
    auto rice = chl::emit(chl::prepare(chl::Image{im.px.data(), 256, 256, 3}, 5, true, true, chl::kEntropyRice), 12);
    auto arith = chl::emit(chl::prepare(chl::Image{im.px.data(), 256, 256, 3}, 5, true, true, chl::kEntropyArith), 12);
    CHECK(arith.size() < rice.size());
}

void test_lossy_quality() {
    Img im = make_image(320, 200, 3, 3, false);
    double last = 1e9;
    size_t last_len = 0;
    for (int q : {95, 80, 50, 20}) {
        chl_options o = defaults();
        o.quality = q;
        std::string s = encode(im, o);
        Img out{};
        CHECK(decode(s, out));
        double p = psnr(im, out);
        CHECK(p > 24.0);
        CHECK(p <= last + 0.5);
        if (last_len) CHECK(s.size() <= last_len);
        last = p;
        last_len = s.size();
    }
}

void test_max_chars() {
    Img im = make_image(640, 480, 3, 11, true);
    for (uint64_t limit : {200000ull, 20000ull, 3000ull, 500ull}) {
        chl_options o = defaults();
        o.max_chars = limit;
        std::string s = encode(im, o);
        CHECK(!s.empty());
        CHECK(chl_char_count(s.data(), s.size()) <= limit);
        Img out{};
        CHECK(decode(s, out));
    }
    chl_options o = defaults();
    o.max_chars = 10;
    int32_t st = 0;
    encode(im, o, &st);
    CHECK(st == CHL_ERR_TOO_BIG);
    o.max_chars = 3000;
    o.allow_resize = 0;
    std::string s = encode(im, o, &st);
    CHECK(st == CHL_OK || st == CHL_ERR_TOO_BIG);
    if (st == CHL_OK) {
        Img out{};
        CHECK(decode(s, out) && out.w == 640 && out.h == 480);
    }
}

void test_corruption() {
    Img im = make_image(64, 48, 3, 5, true);
    chl_options o = defaults();
    std::string s = encode(im, o);
    Img out{};
    int32_t st;

    std::string bad = s;
    bad[bad.size() / 2 - (bad.size() / 2 - 5) % 3] ^= 0x01;  // flip a bit inside a character
    CHECK(!decode(bad, out, &st));

    CHECK(!decode(s.substr(0, s.size() - 3), out, &st));
    CHECK(!decode("HELLO" + s.substr(5), out, &st) && st == CHL_ERR_PREFIX);
    CHECK(!decode("CHL1:abc", out, &st) && st == CHL_ERR_TEXT);

    // Whitespace and newlines inside are fine.
    std::string spaced = s.substr(0, 5) + "\n  " + s.substr(5, 30) + "\r\n" + s.substr(35) + "\n";
    CHECK(decode(spaced, out));

    // Packed file given to decode -> wrong mode.
    char* t = nullptr;
    size_t n = 0;
    const uint8_t data[] = {1, 2, 3};
    CHECK(chl_pack(data, 3, CHL_TEXT_UNICODE, &t, &n) == CHL_OK);
    CHECK(!decode(std::string(t, n), out, &st) && st == CHL_ERR_WRONG_MODE);
    chl_free(t);
}

void test_version_and_flags() {
    Img im = make_image(8, 8, 3, 9, false);
    uint8_t* bin = nullptr;
    size_t n = 0;
    chl_options o = defaults();
    CHECK(chl_encode_binary(im.px.data(), 8, 8, 3, &o, &bin, &n) == CHL_OK);
    std::vector<uint8_t> b(bin, bin + n);
    chl_free(bin);

    auto fix_crc = [](std::vector<uint8_t>& v) {
        uint32_t c = chl::crc32(v.data(), v.size() - 4);
        for (int i = 0; i < 4; ++i) v[v.size() - 4 + size_t(i)] = uint8_t(c >> (24 - 8 * i));
    };
    uint8_t* px = nullptr;
    uint32_t w, h;
    int32_t ch;
    std::vector<uint8_t> v2 = b;
    v2[2] = 0x21;  // version 2
    fix_crc(v2);
    CHECK(chl_decode_binary(v2.data(), v2.size(), &px, &w, &h, &ch) == CHL_ERR_UNSUPPORTED);
    std::vector<uint8_t> rf = b;
    rf[5] |= 0x80;  // flags byte for an 8x8 image sits at offset 5
    fix_crc(rf);
    CHECK(chl_decode_binary(rf.data(), rf.size(), &px, &w, &h, &ch) == CHL_ERR_UNSUPPORTED);
    std::vector<uint8_t> extra = b;
    extra.push_back(0);
    CHECK(chl_decode_binary(extra.data(), extra.size(), &px, &w, &h, &ch) == CHL_ERR_FORMAT);
    CHECK(chl_decode_binary(b.data(), b.size(), &px, &w, &h, &ch) == CHL_OK);
    chl_free(px);
}

void test_pack() {
    std::mt19937 rng(42);
    for (size_t len : {size_t(0), size_t(1), size_t(2), size_t(3), size_t(13), size_t(1000), size_t(65537)}) {
        std::vector<uint8_t> f(len);
        for (auto& x : f) x = uint8_t(rng());
        if (len >= 3) f[0] = 0xFF, f[1] = 0xD8, f[2] = 0xFF;
        for (int mode = 0; mode < 2; ++mode) {
            char* t = nullptr;
            size_t n = 0;
            CHECK(chl_pack(f.data(), f.size(), mode, &t, &n) == CHL_OK);
            uint8_t* back = nullptr;
            size_t bn = 0;
            uint32_t kind = 99;
            CHECK(chl_unpack(t, n, &back, &bn, &kind) == CHL_OK);
            CHECK(bn == len && (len == 0 || std::memcmp(back, f.data(), len) == 0));
            CHECK(kind == (len >= 3 ? 1u : 0u));
            chl_free(back);
            chl_free(t);
        }
    }
}

void test_text_api() {
    std::mt19937 rng(1);
    for (size_t len = 0; len < 64; ++len) {
        std::vector<uint8_t> d(len);
        for (auto& x : d) x = uint8_t(rng());
        for (int mode = 0; mode < 2; ++mode) {
            std::string t = chl::text_encode(d.data(), d.size(), chl_text_mode(mode));
            CHECK(chl_char_count(t.data(), t.size()) == chl::text_chars(len, chl_text_mode(mode)));
            bool uni;
            chl::Bytes back = chl::text_decode(t.data(), t.size(), &uni);
            CHECK(back.size() >= len && back.size() <= len + 1);
            CHECK(std::equal(d.begin(), d.end(), back.begin()));
        }
    }
}

void test_determinism_and_info() {
    Img im = make_image(100, 60, 4, 21, true);
    chl_options o = defaults();
    std::string a = encode(im, o), b = encode(im, o);
    CHECK(!a.empty() && a == b);
    chl_info info;
    CHECK(chl_get_info(a.data(), a.size(), &info) == CHL_OK);
    CHECK(info.mode == 1 && info.width == 100 && info.height == 60 && info.channels == 4);
    CHECK(info.chars == chl_char_count(a.data(), a.size()));
}

void test_bad_arguments() {
    uint8_t px[12] = {0};
    char* t = nullptr;
    size_t n;
    chl_options o = defaults();
    CHECK(chl_encode(nullptr, 2, 2, 3, &o, &t, &n) == CHL_ERR_ARGUMENT);
    CHECK(chl_encode(px, 0, 2, 3, &o, &t, &n) == CHL_ERR_ARGUMENT);
    CHECK(chl_encode(px, 2, 2, 5, &o, &t, &n) == CHL_ERR_ARGUMENT);
    o.levels = 16;
    CHECK(chl_encode(px, 2, 2, 3, &o, &t, &n) == CHL_ERR_ARGUMENT);
    CHECK(chl_encode(px, 100000, 100000, 3, nullptr, &t, &n) == CHL_ERR_LIMIT);
}

}  // namespace

int main() {
    test_spec_example();
    test_wavelet_random();
    test_lossless_roundtrip();
    test_both_entropy_modes();
    test_lossy_quality();
    test_max_chars();
    test_corruption();
    test_version_and_flags();
    test_pack();
    test_text_api();
    test_determinism_and_info();
    test_bad_arguments();
    std::printf("%d passed, %d failed\n", g_passed, g_failed);
    return g_failed ? 1 : 0;
}
