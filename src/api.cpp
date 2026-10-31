// Public C API.
// SPDX-License-Identifier: MIT
#include "internal.hpp"

#include <algorithm>
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
constexpr double kScaleFactor = 0.8;     // each smaller candidate is 80% of the previous size
constexpr double kRefPixels = 1.0e6;     // quality is judged on a copy of at most 1 megapixel
constexpr double kStartBpp = 0.3;        // first size tried gets about this many bits per pixel
constexpr uint32_t kFirstGuessStep = 24; // first step tried when there is no hint

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

// Largest package (bytes) whose text fits in `limit` characters.
uint64_t max_package_bytes(uint64_t limit, chl_text_mode mode) {
    uint64_t prefix = mode == CHL_TEXT_ASCII ? 6 : 5, bits = mode == CHL_TEXT_ASCII ? 6 : 14;
    if (limit <= prefix) return 0;
    uint64_t n = (limit - prefix) * bits / 8;
    while (n > 0 && text_chars(n, mode) > limit) --n;
    return n;
}

// Finds (within ~3%) the smallest step whose package is at most max_bytes. Package size falls
// roughly as a power of the step, so the search runs regula falsi (Illinois) on log(size) vs
// log(step) and needs only a few encodes. `hint` is a good first guess (0 = none). Empty result
// means nothing fits; then *coarsest_size is the size at the coarsest step.
Bytes fit_step(const Prepared& prep, uint32_t step_min, uint32_t hint, uint64_t max_bytes, size_t* coarsest_size) {
    const uint32_t step_max = step_min > kSearchMaxStep ? step_min : kSearchMaxStep;
    const double target = std::log(double(max_bytes > 0 ? max_bytes : 1));
    uint32_t bad = 0, good = 0;  // bad: too big, good: fits
    double f_bad = 0, f_good = 0;
    int last_side = 0;
    Bytes good_bytes;
    auto probe = [&](uint32_t st) {
        Bytes t = emit(prep, st);
        double f = std::log(double(t.size())) - target;
        if (st == step_max && coarsest_size) *coarsest_size = t.size();
        if (t.size() <= max_bytes) {
            good = st;
            f_good = f;
            good_bytes = std::move(t);
            if (last_side == 1) f_bad /= 2;
            last_side = 1;
            return true;
        }
        bad = st;
        f_bad = f;
        if (last_side == -1) f_good /= 2;
        last_side = -1;
        return false;
    };

    // 1. Bracket the answer, starting from the hint.
    uint32_t s = hint ? hint : std::max<uint32_t>(step_min, kFirstGuessStep);
    s = std::min(std::max(s, step_min), step_max);
    if (probe(s)) {
        while (good > step_min) {
            uint32_t down = std::max(step_min, good / 4);
            if (!probe(down)) break;
        }
        if (good == step_min) return good_bytes;  // the requested quality already fits
    } else {
        while (bad < step_max) {
            uint32_t up = std::min<uint64_t>(step_max, uint64_t(bad) * 4);
            if (probe(up)) break;
        }
        if (!good) return Bytes();
    }
    last_side = 0;

    // 2. Narrow it down.
    for (int i = 0; i < 20 && good > bad + 1 && double(good) > double(bad) * 1.03; ++i) {
        double xb = std::log(double(bad)), xg = std::log(double(good));
        double guess = std::sqrt(double(bad) * double(good));
        if (f_bad > 0 && f_good <= 0 && f_bad - f_good > 1e-12)
            guess = std::exp((xb * f_good - xg * f_bad) / (f_good - f_bad));
        uint32_t st = uint32_t(guess + 0.5);
        if (st <= bad) st = bad + 1;
        if (st >= good) st = good - 1;
        probe(st);
    }
    return good_bytes;
}

// Pixels as RGBA, so images with different channel counts can be compared.
Bytes to_rgba(const uint8_t* px, size_t n, int ch) {
    Bytes out(n * 4);
    const size_t c = size_t(ch);
    for (size_t i = 0; i < n; ++i) {
        const uint8_t* a = px + i * c;
        uint8_t* o = &out[i * 4];
        o[0] = a[0];
        o[1] = c >= 3 ? a[1] : a[0];
        o[2] = c >= 3 ? a[2] : a[0];
        o[3] = (c == 2 || c == 4) ? a[c - 1] : 255;
    }
    return out;
}

// The original, at most kRefPixels big, that every candidate is compared against.
struct Reference {
    Bytes rgba;
    uint32_t w, h;
};

Reference make_reference(const uint8_t* px, uint32_t w, uint32_t h, int ch) {
    Reference r{Bytes(), w, h};
    double area = double(w) * h;
    if (area > kRefPixels) {
        double s = std::sqrt(kRefPixels / area);
        r.w = std::max<uint32_t>(1, uint32_t(w * s));
        r.h = std::max<uint32_t>(1, uint32_t(h * s));
        Bytes small = resize_area(px, w, h, ch, r.w, r.h);
        r.rgba = to_rgba(small.data(), size_t(r.w) * r.h, ch);
    } else {
        r.rgba = to_rgba(px, size_t(w) * h, ch);
    }
    return r;
}

// Error of a candidate as the viewer sees it: decoded and scaled to the reference size.
double candidate_error(const Bytes& pkg, const Reference& ref) {
    Header hd = parse_header(pkg.data(), pkg.size());
    Decoded d = decode_codec(hd, pkg.data());
    Bytes rgba = to_rgba(d.px.data(), size_t(d.w) * d.h, d.channels);
    if (d.w != ref.w || d.h != ref.h) {
        bool shrink = uint64_t(d.w) * d.h > uint64_t(ref.w) * ref.h;
        rgba = shrink ? resize_area(rgba.data(), d.w, d.h, 4, ref.w, ref.h)
                      : resize_bilinear(rgba.data(), d.w, d.h, 4, ref.w, ref.h);
    }
    double se = 0;
    for (size_t i = 0; i < rgba.size(); ++i) {
        double e = double(rgba[i]) - ref.rgba[i];
        se += e * e;
    }
    return se;
}

// Tries the full size and smaller sizes, keeps the one that looks best at the original size.
Bytes encode_fit(const uint8_t* px, uint32_t w, uint32_t h, int ch, const chl_options& o) {
    const chl_text_mode mode = chl_text_mode(o.text_mode);
    const uint64_t limit = o.max_chars;
    const uint32_t step_min = step_for(o);
    const bool chroma = o.chroma_420 < 0 ? true : o.chroma_420 != 0;
    const double budget_bits = double(limit) * (mode == CHL_TEXT_ASCII ? 6.0 : 14.0);
    const uint64_t max_bytes = max_package_bytes(limit, mode);
    const Reference ref = make_reference(px, w, h, ch);

    struct Candidate {
        bool fits = false;
        double err = 0;
        uint32_t step = 0;
        size_t coarsest = 0;
        Bytes pkg;
    };
    uint32_t hint = 0;
    auto try_scale = [&](double scale) {
        Candidate c;
        uint32_t sw = uint32_t(std::lround(w * scale)), sh = uint32_t(std::lround(h * scale));
        sw = sw < 1 ? 1 : (sw > w ? w : sw);
        sh = sh < 1 ? 1 : (sh > h ? h : sh);
        Bytes resized;
        const uint8_t* src = px;
        if (sw != w || sh != h) {
            resized = resize_area(px, w, h, ch, sw, sh);
            src = resized.data();
        }
        c.pkg = fit_step(prepare(Image{src, sw, sh, ch}, o.levels, chroma, o.alpha_lossless != 0), step_min, hint,
                         max_bytes, &c.coarsest);
        if (!c.pkg.empty()) {
            c.fits = true;
            c.step = parse_header(c.pkg.data(), c.pkg.size()).base_step;
            c.err = candidate_error(c.pkg, ref);
            hint = c.step;
        }
        return c;
    };
    auto too_small = [&](double scale) { return w * scale < 8 && h * scale < 8; };

    // Start where the budget gives about kStartBpp bits per pixel.
    double scale = 1.0;
    if (o.allow_resize) {
        double bpp = budget_bits / (double(w) * h);
        if (bpp < kStartBpp) scale = std::sqrt(bpp / kStartBpp);
    }
    Candidate best = try_scale(scale);
    if (!o.allow_resize) {
        if (!best.fits) fail(CHL_ERR_TOO_BIG, "does not fit in max_chars");
        return std::move(best.pkg);
    }
    // Shrink until something fits.
    while (!best.fits && !too_small(scale)) {
        double jump = best.coarsest ? std::sqrt(budget_bits / 8.0 / double(best.coarsest)) * 0.95 : kScaleFactor;
        scale *= std::max(0.05, std::min(kScaleFactor, jump));
        best = try_scale(scale);
    }
    if (!best.fits) fail(CHL_ERR_TOO_BIG, "does not fit in max_chars");
    double best_scale = scale;

    // Walk down while it looks better; if the first step down is worse, walk up instead.
    bool moved_down = false;
    for (double s2 = best_scale * kScaleFactor; best.err > 0 && !too_small(s2); s2 *= kScaleFactor) {
        Candidate c = try_scale(s2);
        if (!c.fits || c.err >= best.err) break;
        best = std::move(c);
        best_scale = s2;
        moved_down = true;
    }
    if (!moved_down) {
        hint = best.step;
        for (double s2 = best_scale / kScaleFactor; best.err > 0 && best_scale < 1.0; s2 /= kScaleFactor) {
            if (s2 > 1.0) s2 = 1.0;
            Candidate c = try_scale(s2);
            if (!c.fits || c.err >= best.err) break;
            best = std::move(c);
            best_scale = s2;
        }
    }
    return std::move(best.pkg);
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

CHL_API const char* chl_version(void) { return "1.1.0"; }

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
