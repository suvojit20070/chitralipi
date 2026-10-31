// Public C API.
// SPDX-License-Identifier: MIT
#include "internal.hpp"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <new>

using namespace chl;

namespace {

// Quality 0..100 -> base step, round(2^((100 - q) / 12.5)), at least 2 below 100.
// A table (not pow) so every platform makes the same string.
const uint16_t kQualityStep[101] = {
    256, 242, 229, 217, 205, 194, 184, 174, 164, 155, 147, 139, 132, 124, 118, 111, 105, 100, 94, 89, 84,
    80,  76,  72,  68,  64,  61,  57,  54,  51,  49,  46,  43,  41,  39,  37,  35,  33,  31,  29,  28,
    26,  25,  24,  22,  21,  20,  19,  18,  17,  16,  15,  14,  14,  13,  12,  11,  11,  10,  10,  9,
    9,   8,   8,   7,   7,   7,   6,   6,   6,   5,   5,   5,   4,   4,   4,   4,   4,   3,   3,   3,
    3,   3,   3,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   1};

constexpr uint32_t kSearchMaxStep = 4096;
constexpr uint32_t kResizeStepCap = 40;  // above this a smaller image usually looks better
constexpr double kMinBitsPerPixel = 0.25;

template <class F>
int32_t guard(F&& f) {
    try {
        f();
        return CHL_OK;
    } catch (const Error& e) {
        return e.code;
    } catch (const std::bad_alloc&) {
        return CHL_ERR_NOMEM;
    } catch (...) {
        return CHL_ERR_INTERNAL;
    }
}

char* copy_string(const std::string& s) {
    char* out = static_cast<char*>(std::malloc(s.size() + 1));
    if (!out) throw std::bad_alloc();
    std::memcpy(out, s.data(), s.size());
    out[s.size()] = '\0';
    return out;
}

uint8_t* copy_bytes(const uint8_t* d, size_t n) {
    uint8_t* out = static_cast<uint8_t*>(std::malloc(n ? n : 1));
    if (!out) throw std::bad_alloc();
    if (n) std::memcpy(out, d, n);
    return out;
}

// Strict end check: binary must end exactly; Unicode text may add one zero padding byte.
void check_tail(const Bytes& b, const Header& h, bool from_unicode_text) {
    size_t extra = b.size() - h.total;
    if (extra == 0) return;
    if (from_unicode_text && extra == 1 && b.back() == 0) return;
    fail(CHL_ERR_FORMAT, "extra bytes after package");
}

uint32_t step_for(const chl_options& o) {
    if (o.base_step > 0) return uint32_t(o.base_step);
    int q = o.quality < 0 ? 0 : (o.quality > 100 ? 100 : o.quality);
    return kQualityStep[q];
}

void validate(const uint8_t* px, uint32_t w, uint32_t h, int32_t ch, const chl_options& o) {
    if (!px || w == 0 || h == 0 || ch < 1 || ch > 4) fail(CHL_ERR_ARGUMENT, "bad image");
    if (uint64_t(w) * h > kMaxPixels) fail(CHL_ERR_LIMIT, "image larger than the safety limit");
    if (o.text_mode != CHL_TEXT_UNICODE && o.text_mode != CHL_TEXT_ASCII) fail(CHL_ERR_ARGUMENT, "bad text_mode");
    if (o.levels < 0 || o.levels > kMaxLevels) fail(CHL_ERR_ARGUMENT, "levels must be 0..15");
    if (o.base_step < 0 || uint32_t(o.base_step) > kMaxBaseStep) fail(CHL_ERR_ARGUMENT, "bad base_step");
    if (o.chroma_420 < -1 || o.chroma_420 > 1 || o.reserved != 0) fail(CHL_ERR_ARGUMENT, "bad option");
}

Bytes encode_fit(const uint8_t* px, uint32_t w, uint32_t h, int ch, const chl_options& o) {
    const chl_text_mode mode = chl_text_mode(o.text_mode);
    const uint64_t limit = o.max_chars;
    const uint32_t step_min = step_for(o);
    const bool chroma = o.chroma_420 < 0 ? true : o.chroma_420 != 0;
    auto fits = [&](const Bytes& b) { return text_chars(b.size(), mode) <= limit; };

    double scale = 1.0;
    if (o.allow_resize) {
        double bits = double(limit) * (mode == CHL_TEXT_ASCII ? 6.0 : 14.0);
        double bpp = bits / (double(w) * h);
        if (bpp < kMinBitsPerPixel) scale = std::sqrt(bpp / kMinBitsPerPixel);
    }

    Bytes fallback;
    for (int round = 0; round < 64; ++round) {
        uint32_t sw = uint32_t(std::lround(w * scale)), sh = uint32_t(std::lround(h * scale));
        if (sw < 1) sw = 1;
        if (sh < 1) sh = 1;
        if (sw > w) sw = w;
        if (sh > h) sh = h;
        Bytes resized;
        const uint8_t* src = px;
        if (sw != w || sh != h) {
            resized = resize_area(px, w, h, ch, sw, sh);
            src = resized.data();
        }
        Prepared prep = prepare(Image{src, sw, sh, ch}, o.levels, chroma, o.alpha_lossless != 0);

        uint32_t hi = step_min > kSearchMaxStep ? step_min : kSearchMaxStep;
        Bytes best = emit(prep, hi);
        bool ok = fits(best);
        uint32_t found = hi;
        if (ok) {
            uint32_t lo = step_min;
            while (lo < hi) {
                uint32_t mid = lo + (hi - lo) / 2;
                Bytes b = emit(prep, mid);
                if (fits(b)) {
                    hi = mid;
                    best = std::move(b);
                } else {
                    lo = mid + 1;
                }
            }
            found = hi;
        }
        if (ok && (!o.allow_resize || found <= kResizeStepCap)) return best;
        if (ok && fallback.empty()) fallback = std::move(best);
        if (!o.allow_resize) fail(CHL_ERR_TOO_BIG, "does not fit in max_chars");
        if (sw <= 8 && sh <= 8) break;
        scale *= 0.8;
    }
    if (!fallback.empty()) return fallback;
    fail(CHL_ERR_TOO_BIG, "does not fit in max_chars");
}

Bytes encode_any(const uint8_t* px, uint32_t w, uint32_t h, int32_t ch, const chl_options* in) {
    chl_options o;
    chl_options_default(&o);
    if (in) o = *in;
    validate(px, w, h, ch, o);
    if (o.max_chars > 0) return encode_fit(px, w, h, ch, o);
    uint32_t step = step_for(o);
    bool chroma = o.chroma_420 < 0 ? step > 1 : o.chroma_420 != 0;
    return emit(prepare(Image{px, w, h, ch}, o.levels, chroma, o.alpha_lossless != 0), step);
}

void give_pixels(const Decoded& d, uint8_t** px, uint32_t* w, uint32_t* h, int32_t* ch) {
    *px = copy_bytes(d.px.data(), d.px.size());
    if (w) *w = d.w;
    if (h) *h = d.h;
    if (ch) *ch = d.channels;
}

}  // namespace

extern "C" {

CHL_API const char* chl_version(void) { return "1.0.0"; }

CHL_API const char* chl_status_string(int32_t s) {
    switch (s) {
        case CHL_OK: return "ok";
        case CHL_ERR_ARGUMENT: return "invalid argument";
        case CHL_ERR_NOMEM: return "out of memory";
        case CHL_ERR_PREFIX: return "text does not start with CHL1: or CHL1a:";
        case CHL_ERR_TEXT: return "text has an illegal character or bad padding";
        case CHL_ERR_FORMAT: return "package is broken or truncated";
        case CHL_ERR_CHECKSUM: return "checksum mismatch, the text was changed or cut";
        case CHL_ERR_UNSUPPORTED: return "made by a newer Chitralipi version";
        case CHL_ERR_LIMIT: return "larger than the safety limit";
        case CHL_ERR_TOO_BIG: return "cannot fit in max_chars";
        case CHL_ERR_WRONG_MODE: return "wrong kind of package (image vs packed file)";
        default: return "internal error";
    }
}

CHL_API void chl_options_default(chl_options* o) {
    if (!o) return;
    std::memset(o, 0, sizeof(*o));
    o->quality = 80;
    o->levels = kDefaultLevels;
    o->chroma_420 = -1;
    o->alpha_lossless = 1;
    o->text_mode = CHL_TEXT_UNICODE;
    o->allow_resize = 1;
}

CHL_API void chl_free(void* p) { std::free(p); }

CHL_API uint64_t chl_char_count(const char* t, size_t n) { return t ? utf8_count(t, n) : 0; }

CHL_API int32_t chl_encode_binary(const uint8_t* px, uint32_t w, uint32_t h, int32_t ch, const chl_options* o,
                                  uint8_t** out, size_t* out_len) {
    return guard([&] {
        if (!out) fail(CHL_ERR_ARGUMENT, "null output");
        Bytes b = encode_any(px, w, h, ch, o);
        *out = copy_bytes(b.data(), b.size());
        if (out_len) *out_len = b.size();
    });
}

CHL_API int32_t chl_encode(const uint8_t* px, uint32_t w, uint32_t h, int32_t ch, const chl_options* o,
                           char** out, size_t* out_len) {
    return guard([&] {
        if (!out) fail(CHL_ERR_ARGUMENT, "null output");
        chl_text_mode mode = o ? chl_text_mode(o->text_mode) : CHL_TEXT_UNICODE;
        Bytes b = encode_any(px, w, h, ch, o);
        std::string s = text_encode(b.data(), b.size(), mode);
        *out = copy_string(s);
        if (out_len) *out_len = s.size();
    });
}

CHL_API int32_t chl_decode_binary(const uint8_t* bytes, size_t len, uint8_t** px, uint32_t* w, uint32_t* h,
                                  int32_t* ch) {
    return guard([&] {
        if (!bytes || !px) fail(CHL_ERR_ARGUMENT, "null pointer");
        Header hd = parse_header(bytes, len);
        if (hd.mode != kModeCodec) fail(CHL_ERR_WRONG_MODE, "this is a packed file, use unpack");
        if (len != hd.total) fail(CHL_ERR_FORMAT, "extra bytes after package");
        give_pixels(decode_codec(hd, bytes), px, w, h, ch);
    });
}

CHL_API int32_t chl_decode(const char* text, size_t len, uint8_t** px, uint32_t* w, uint32_t* h, int32_t* ch) {
    return guard([&] {
        if (!text || !px) fail(CHL_ERR_ARGUMENT, "null pointer");
        bool uni = false;
        Bytes b = text_decode(text, len, &uni);
        Header hd = parse_header(b.data(), b.size());
        if (hd.mode != kModeCodec) fail(CHL_ERR_WRONG_MODE, "this is a packed file, use unpack");
        check_tail(b, hd, uni);
        give_pixels(decode_codec(hd, b.data()), px, w, h, ch);
    });
}

CHL_API int32_t chl_pack(const uint8_t* file, size_t len, int32_t text_mode, char** out, size_t* out_len) {
    return guard([&] {
        if ((!file && len) || !out) fail(CHL_ERR_ARGUMENT, "null pointer");
        if (text_mode != CHL_TEXT_UNICODE && text_mode != CHL_TEXT_ASCII) fail(CHL_ERR_ARGUMENT, "bad text_mode");
        Bytes b = build_store(file, len, detect_file_kind(file, len));
        std::string s = text_encode(b.data(), b.size(), chl_text_mode(text_mode));
        *out = copy_string(s);
        if (out_len) *out_len = s.size();
    });
}

CHL_API int32_t chl_unpack(const char* text, size_t len, uint8_t** out, size_t* out_len, uint32_t* kind) {
    return guard([&] {
        if (!text || !out) fail(CHL_ERR_ARGUMENT, "null pointer");
        bool uni = false;
        Bytes b = text_decode(text, len, &uni);
        Header hd = parse_header(b.data(), b.size());
        if (hd.mode != kModeStore) fail(CHL_ERR_WRONG_MODE, "this is an image, use decode");
        check_tail(b, hd, uni);
        *out = copy_bytes(b.data() + hd.data_off, size_t(hd.data_len));
        if (out_len) *out_len = size_t(hd.data_len);
        if (kind) *kind = hd.file_kind;
    });
}

CHL_API int32_t chl_text_encode(const uint8_t* bytes, size_t len, int32_t text_mode, char** out, size_t* out_len) {
    return guard([&] {
        if ((!bytes && len) || !out) fail(CHL_ERR_ARGUMENT, "null pointer");
        if (text_mode != CHL_TEXT_UNICODE && text_mode != CHL_TEXT_ASCII) fail(CHL_ERR_ARGUMENT, "bad text_mode");
        std::string s = text_encode(bytes, len, chl_text_mode(text_mode));
        *out = copy_string(s);
        if (out_len) *out_len = s.size();
    });
}

CHL_API int32_t chl_text_decode(const char* text, size_t len, uint8_t** out, size_t* out_len) {
    return guard([&] {
        if (!text || !out) fail(CHL_ERR_ARGUMENT, "null pointer");
        bool uni = false;
        Bytes b = text_decode(text, len, &uni);
        // Drop the Unicode padding byte when the bytes are a Chitralipi package.
        if (uni && !b.empty() && b.back() == 0) {
            try {
                Header hd = parse_header(b.data(), b.size());
                if (b.size() == hd.total + 1) b.pop_back();
            } catch (const Error&) {
            }
        }
        *out = copy_bytes(b.data(), b.size());
        if (out_len) *out_len = b.size();
    });
}

CHL_API int32_t chl_get_info(const char* text, size_t len, chl_info* info) {
    return guard([&] {
        if (!text || !info) fail(CHL_ERR_ARGUMENT, "null pointer");
        bool uni = false;
        Bytes b = text_decode(text, len, &uni);
        Header hd = parse_header(b.data(), b.size());
        check_tail(b, hd, uni);
        std::memset(info, 0, sizeof(*info));
        info->version = hd.version;
        info->mode = hd.mode;
        info->width = hd.w;
        info->height = hd.h;
        info->levels = hd.levels;
        info->base_step = hd.base_step;
        info->file_kind = hd.file_kind;
        info->flags = hd.flags;
        info->channels = hd.mode == kModeCodec ? output_channels(hd.flags) : 0;
        info->data_bytes = hd.data_len;
        info->package_bytes = hd.total;
        info->chars = utf8_count(text, len);
    });
}

}  // extern "C"
