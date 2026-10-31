// Chitralipi internals. Not part of the public API.
// SPDX-License-Identifier: MIT
#pragma once

#include "chitralipi/chitralipi.h"

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

static_assert((-3 >> 1) == -2, "Chitralipi needs arithmetic (floor) right shift");

namespace chl {

using Bytes = std::vector<uint8_t>;

struct Error : std::runtime_error {
    chl_status code;
    Error(chl_status c, const char* msg) : std::runtime_error(msg), code(c) {}
};

[[noreturn]] inline void fail(chl_status code, const char* msg) { throw Error(code, msg); }

// ---- format constants -------------------------------------------------------
constexpr uint8_t kMagic0 = 0x43;  // 'C'
constexpr uint8_t kMagic1 = 0x4C;  // 'L'
constexpr uint8_t kVersion = 1;
constexpr uint8_t kModeStore = 0;
constexpr uint8_t kModeCodec = 1;

constexpr uint64_t kMaxPixels = 1ull << 26;      // 67 megapixels
constexpr uint64_t kMaxStoreBytes = 1ull << 31;  // 2 GiB
constexpr uint32_t kMaxBaseStep = 1u << 20;
constexpr int kMaxLevels = 15;
constexpr int kDefaultLevels = 5;

enum : uint8_t {
    F_ALPHA = 1,
    F_CHROMA420 = 2,
    F_GRAY = 4,
    F_ALPHA_LOSSLESS = 8,
    F_ENTROPY_AC = 0x10,  // bits 4-5 = entropy mode: 0 run+Rice, 1 arithmetic coding
    F_UNKNOWN = 0xE0,     // entropy modes 2-3 and bits 6-7 are not defined
};

constexpr int kEntropyRice = 0;
constexpr int kEntropyArith = 1;

// ---- helpers (util.cpp) -----------------------------------------------------
uint32_t crc32(const uint8_t* data, size_t len);
void put_varint(Bytes& out, uint64_t value);
uint64_t get_varint(const uint8_t* data, size_t len, size_t& pos);
Bytes resize_area(const uint8_t* px, uint32_t w, uint32_t h, int channels, uint32_t nw, uint32_t nh);
Bytes resize_bilinear(const uint8_t* px, uint32_t w, uint32_t h, int channels, uint32_t nw, uint32_t nh);
uint32_t detect_file_kind(const uint8_t* data, size_t len);

// ---- planes and transforms (transform.cpp) ----------------------------------
struct Plane {
    uint32_t w = 0, h = 0;
    std::vector<int32_t> v;
    Plane() = default;
    Plane(uint32_t width, uint32_t height) : w(width), h(height), v(size_t(width) * height, 0) {}
    int32_t& at(uint32_t x, uint32_t y) { return v[size_t(y) * w + x]; }
    int32_t at(uint32_t x, uint32_t y) const { return v[size_t(y) * w + x]; }
};

inline void ycocg_forward(int32_t r, int32_t g, int32_t b, int32_t& y, int32_t& co, int32_t& cg) {
    co = r - b;
    int32_t t = b + (co >> 1);
    cg = g - t;
    y = t + (cg >> 1);
}

inline void ycocg_inverse(int32_t y, int32_t co, int32_t cg, int32_t& r, int32_t& g, int32_t& b) {
    int32_t t = y - (cg >> 1);
    g = cg + t;
    b = t - (co >> 1);
    r = b + co;
}

Plane chroma_down(const Plane& p);
Plane chroma_up(const Plane& p, uint32_t w, uint32_t h);
void dwt_forward(Plane& p, int levels);
void dwt_inverse(Plane& p, int levels);

enum BandKind { BAND_LL = 0, BAND_HL = 1, BAND_LH = 2, BAND_HH = 3 };

struct Band {
    uint32_t x0, y0, w, h;
    int level;  // 1 = finest; LL carries the deepest level
    BandKind kind;
};

std::vector<Band> band_layout(uint32_t w, uint32_t h, int levels);
uint32_t band_step(uint32_t base, const Band& band, int levels);

// ---- codec (codec.cpp) ------------------------------------------------------
struct Image {
    const uint8_t* px;
    uint32_t w, h;
    int channels;
};

struct Prepared {
    uint32_t w = 0, h = 0;
    uint8_t flags = 0;
    int levels = 0;
    std::vector<Plane> planes;   // already wavelet transformed
    std::vector<int> plane_role; // 0 luma, 1 chroma, 2 alpha
};

Prepared prepare(const Image& img, int levels, bool chroma420, bool alpha_lossless, int entropy = kEntropyArith);
Bytes emit(const Prepared& prep, uint32_t base_step);

struct Header {
    uint8_t version = 0;
    uint8_t mode = 0;
    uint32_t w = 0, h = 0;
    uint8_t flags = 0;
    int levels = 0;
    uint32_t base_step = 0;
    uint32_t file_kind = 0;
    uint64_t data_len = 0;
    size_t data_off = 0;
    size_t total = 0;  // package bytes including CRC
};

// Parses and validates the header and CRC. Bytes after `total` are not checked here.
Header parse_header(const uint8_t* data, size_t len);

struct Decoded {
    uint32_t w = 0, h = 0;
    int channels = 0;
    Bytes px;
};

Decoded decode_codec(const Header& hd, const uint8_t* data);
Bytes build_store(const uint8_t* data, size_t len, uint32_t kind);
int output_channels(uint8_t flags);

// ---- text envelope (text.cpp) -----------------------------------------------
extern const char* const kPrefixUnicode;  // "CHL1:"
extern const char* const kPrefixAscii;    // "CHL1a:"

std::string text_encode(const uint8_t* data, size_t len, chl_text_mode mode);
Bytes text_decode(const char* text, size_t len, bool* was_unicode);
uint64_t text_chars(uint64_t nbytes, chl_text_mode mode);
uint64_t utf8_count(const char* text, size_t len);

}  // namespace chl
