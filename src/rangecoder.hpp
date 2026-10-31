// Binary adaptive range coder (entropy mode 1). Integer only, identical on every platform.
// SPDX-License-Identifier: MIT
#pragma once

#include "internal.hpp"

namespace chl {

constexpr int kProbBits = 11;
constexpr uint16_t kProbInit = 1u << (kProbBits - 1);  // P(bit = 0) = 1/2
constexpr int kProbShift = 5;
constexpr uint32_t kTop = 1u << 24;

class RangeEncoder {
public:
    void bit(uint16_t& p, int b) {
        uint32_t bound = (range_ >> kProbBits) * p;
        if (b == 0) {
            range_ = bound;
            p = uint16_t(p + (((1u << kProbBits) - p) >> kProbShift));
        } else {
            low_ += bound;
            range_ -= bound;
            p = uint16_t(p - (p >> kProbShift));
        }
        while (range_ < kTop) {
            range_ <<= 8;
            shift_low();
        }
    }

    void direct(uint32_t v, int n) {  // n raw bits, most significant first
        for (int i = n - 1; i >= 0; --i) {
            range_ >>= 1;
            if ((v >> i) & 1) low_ += range_;
            while (range_ < kTop) {
                range_ <<= 8;
                shift_low();
            }
        }
    }

    Bytes finish() {
        for (int i = 0; i < 5; ++i) shift_low();
        return std::move(out_);
    }

private:
    void shift_low() {
        if (uint32_t(low_) < 0xFF000000u || (low_ >> 32) != 0) {
            uint8_t carry = uint8_t(low_ >> 32);
            uint8_t temp = cache_;
            do {
                out_.push_back(uint8_t(temp + carry));
                temp = 0xFF;
            } while (--cache_size_ != 0);
            cache_ = uint8_t(low_ >> 24);
        }
        ++cache_size_;
        low_ = (low_ & 0x00FFFFFFu) << 8;
    }

    Bytes out_;
    uint64_t low_ = 0;
    uint32_t range_ = 0xFFFFFFFFu;
    uint8_t cache_ = 0;
    uint64_t cache_size_ = 1;
};

class RangeDecoder {
public:
    RangeDecoder(const uint8_t* p, size_t n) : p_(p), n_(n) {
        for (int i = 0; i < 5; ++i) code_ = (code_ << 8) | next();
    }

    int bit(uint16_t& p) {
        uint32_t bound = (range_ >> kProbBits) * p;
        int b;
        if (code_ < bound) {
            range_ = bound;
            p = uint16_t(p + (((1u << kProbBits) - p) >> kProbShift));
            b = 0;
        } else {
            code_ -= bound;
            range_ -= bound;
            p = uint16_t(p - (p >> kProbShift));
            b = 1;
        }
        while (range_ < kTop) {
            range_ <<= 8;
            code_ = (code_ << 8) | next();
        }
        return b;
    }

    uint32_t direct(int n) {
        uint32_t v = 0;
        for (int i = 0; i < n; ++i) {
            range_ >>= 1;
            uint32_t b = code_ >= range_ ? 1u : 0u;
            if (b) code_ -= range_;
            v = (v << 1) | b;
            while (range_ < kTop) {
                range_ <<= 8;
                code_ = (code_ << 8) | next();
            }
        }
        return v;
    }

    bool clean_end() const { return pos_ == n_; }

private:
    uint32_t next() {
        if (pos_ >= n_) fail(CHL_ERR_FORMAT, "bitstream ended early");
        return p_[pos_++];
    }

    const uint8_t* p_;
    size_t n_;
    size_t pos_ = 0;
    uint32_t code_ = 0;
    uint32_t range_ = 0xFFFFFFFFu;
};

}  // namespace chl
