// MSB-first bit writer and reader.
// SPDX-License-Identifier: MIT
#pragma once

#include "internal.hpp"

namespace chl {

inline uint64_t low_mask(int n) { return n >= 64 ? ~0ull : ((1ull << n) - 1); }

class BitWriter {
public:
    // Writes the lowest n bits of v, most significant first. n is 0..32.
    void put(uint32_t v, int n) {
        if (n == 0) return;
        acc_ = (acc_ << n) | (uint64_t(v) & low_mask(n));
        bits_ += n;
        while (bits_ >= 8) {
            bits_ -= 8;
            out_.push_back(uint8_t(acc_ >> bits_));
        }
        acc_ &= low_mask(bits_);
    }

    void ones(int n) { put(uint32_t(low_mask(n)), n); }  // n <= 32

    // Exp-Golomb order 0.
    void eg0(uint64_t x) {
        uint64_t v = x + 1;
        int b = 0;
        for (uint64_t t = v; t; t >>= 1) ++b;
        for (int z = b - 1; z > 0; z -= 32) put(0, z > 32 ? 32 : z);
        if (b > 32) {
            put(uint32_t(v >> 32), b - 32);
            put(uint32_t(v), 32);
        } else {
            put(uint32_t(v), b);
        }
    }

    // Rice code with escape: 24 ones then the raw 32-bit value.
    void rice(uint32_t m, int k) {
        uint32_t q = m >> k;
        if (q < 24) {
            ones(int(q));
            put(0, 1);
            put(m, k);
        } else {
            ones(24);
            put(m, 32);
        }
    }

    Bytes finish() {
        if (bits_ > 0) {
            out_.push_back(uint8_t(acc_ << (8 - bits_)));
            bits_ = 0;
            acc_ = 0;
        }
        return std::move(out_);
    }

private:
    Bytes out_;
    uint64_t acc_ = 0;
    int bits_ = 0;
};

class BitReader {
public:
    BitReader(const uint8_t* p, size_t n) : p_(p), n_(n) {}

    uint32_t get(int n) {
        if (n == 0) return 0;
        while (bits_ < n) {
            if (pos_ >= n_) fail(CHL_ERR_FORMAT, "bitstream ended early");
            acc_ = (acc_ << 8) | p_[pos_++];
            bits_ += 8;
        }
        bits_ -= n;
        uint32_t v = uint32_t((acc_ >> bits_) & low_mask(n));
        acc_ &= low_mask(bits_);
        return v;
    }

    uint64_t eg0() {
        int zeros = 0;
        while (get(1) == 0) {
            if (++zeros > 62) fail(CHL_ERR_FORMAT, "bad run length");
        }
        uint64_t v = 1;
        for (int left = zeros; left > 0;) {
            int take = left > 32 ? 32 : left;
            v = (v << take) | get(take);
            left -= take;
        }
        return v - 1;
    }

    uint32_t rice(int k) {
        int q = 0;
        while (q < 24 && get(1) == 1) ++q;
        if (q == 24) return get(32);
        return (uint32_t(q) << k) | get(k);
    }

    // True when every byte was used and the padding bits are zero.
    bool clean_end() const { return pos_ == n_ && acc_ == 0; }

private:
    const uint8_t* p_;
    size_t n_;
    size_t pos_ = 0;
    uint64_t acc_ = 0;
    int bits_ = 0;
};

}  // namespace chl
