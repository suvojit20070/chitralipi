// Text envelope: Lipi-14 (Unicode, 14 bits per character) and Base64URL.
// SPDX-License-Identifier: MIT
#include "internal.hpp"

#include <cstring>

namespace chl {

const char* const kPrefixUnicode = "CHL1:";
const char* const kPrefixAscii = "CHL1a:";

namespace {

constexpr uint32_t kLipiBase = 0x4E00;  // U+4E00 .. U+8DFF, CJK Unified Ideographs
constexpr char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

inline bool is_space(uint32_t c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

void put_cp(std::string& s, uint32_t cp) {
    s.push_back(char(0xE0 | (cp >> 12)));
    s.push_back(char(0x80 | ((cp >> 6) & 0x3F)));
    s.push_back(char(0x80 | (cp & 0x3F)));
}

int b64_value(unsigned char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '-') return 62;
    if (c == '_') return 63;
    return -1;
}

}  // namespace

uint64_t text_chars(uint64_t nbytes, chl_text_mode mode) {
    if (mode == CHL_TEXT_ASCII) return 6 + (nbytes * 8 + 5) / 6;
    return 5 + (nbytes * 8 + 13) / 14;
}

std::string text_encode(const uint8_t* d, size_t n, chl_text_mode mode) {
    std::string s;
    uint64_t acc = 0;
    int bits = 0;
    if (mode == CHL_TEXT_ASCII) {
        s.reserve(6 + (n * 8 + 5) / 6);
        s += kPrefixAscii;
        for (size_t i = 0; i < n; ++i) {
            acc = (acc << 8) | d[i];
            bits += 8;
            while (bits >= 6) {
                bits -= 6;
                s.push_back(kB64[(acc >> bits) & 63]);
            }
            acc &= (1ull << bits) - 1;
        }
        if (bits > 0) s.push_back(kB64[(acc << (6 - bits)) & 63]);
        return s;
    }
    s.reserve(5 + 3 * ((n * 8 + 13) / 14));
    s += kPrefixUnicode;
    for (size_t i = 0; i < n; ++i) {
        acc = (acc << 8) | d[i];
        bits += 8;
        if (bits >= 14) {
            bits -= 14;
            put_cp(s, kLipiBase + uint32_t((acc >> bits) & 0x3FFF));
        }
        acc &= (1ull << bits) - 1;
    }
    if (bits > 0) put_cp(s, kLipiBase + uint32_t((acc << (14 - bits)) & 0x3FFF));
    return s;
}

Bytes text_decode(const char* text, size_t len, bool* was_unicode) {
    const unsigned char* t = reinterpret_cast<const unsigned char*>(text);
    size_t i = 0;
    if (len >= 3 && t[0] == 0xEF && t[1] == 0xBB && t[2] == 0xBF) i = 3;  // UTF-8 BOM
    while (i < len && is_space(t[i])) ++i;

    bool unicode;
    if (len - i >= 6 && std::memcmp(t + i, kPrefixAscii, 6) == 0) {
        unicode = false;
        i += 6;
    } else if (len - i >= 5 && std::memcmp(t + i, kPrefixUnicode, 5) == 0) {
        unicode = true;
        i += 5;
    } else {
        fail(CHL_ERR_PREFIX, "text must start with CHL1: or CHL1a:");
    }
    if (was_unicode) *was_unicode = unicode;

    Bytes out;
    out.reserve(unicode ? (len - i) * 14 / 24 + 1 : (len - i) * 6 / 8 + 1);
    uint64_t acc = 0;
    int bits = 0;
    if (!unicode) {
        size_t symbols = 0;
        for (; i < len; ++i) {
            if (is_space(t[i])) continue;
            int v = b64_value(t[i]);
            if (v < 0) fail(CHL_ERR_TEXT, "illegal character in ASCII text");
            acc = (acc << 6) | uint32_t(v);
            bits += 6;
            ++symbols;
            if (bits >= 8) {
                bits -= 8;
                out.push_back(uint8_t(acc >> bits));
            }
            acc &= (1ull << bits) - 1;
        }
        if (symbols % 4 == 1 || acc != 0) fail(CHL_ERR_TEXT, "bad ASCII padding");
        return out;
    }
    while (i < len) {
        if (is_space(t[i])) {
            ++i;
            continue;
        }
        if (len - i < 3 || (t[i] & 0xF0) != 0xE0 || (t[i + 1] & 0xC0) != 0x80 || (t[i + 2] & 0xC0) != 0x80)
            fail(CHL_ERR_TEXT, "illegal character in text");
        uint32_t cp = (uint32_t(t[i] & 0x0F) << 12) | (uint32_t(t[i + 1] & 0x3F) << 6) | uint32_t(t[i + 2] & 0x3F);
        i += 3;
        if (cp < kLipiBase || cp > kLipiBase + 0x3FFF) fail(CHL_ERR_TEXT, "character outside the Chitralipi range");
        acc = (acc << 14) | (cp - kLipiBase);
        bits += 14;
        while (bits >= 8) {
            bits -= 8;
            out.push_back(uint8_t(acc >> bits));
        }
        acc &= (1ull << bits) - 1;
    }
    if (acc != 0) fail(CHL_ERR_TEXT, "bad padding bits");
    return out;
}

uint64_t utf8_count(const char* text, size_t len) {
    uint64_t n = 0;
    for (size_t i = 0; i < len; ++i)
        if ((static_cast<unsigned char>(text[i]) & 0xC0) != 0x80) ++n;
    return n;
}

}  // namespace chl
