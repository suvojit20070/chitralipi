/*
 * Chitralipi - image to text string, text string to image.
 * Copyright (c) 2026 Suvo (github.com/suvojit20070)
 * SPDX-License-Identifier: MIT
 *
 * Plain C API. Every language (C, C++, Java/JNI, Python ctypes, C#, Go cgo,
 * Rust FFI, Node ffi, WebAssembly) talks to the library through this header.
 *
 * Rules:
 *  - Strings are UTF-8. A Chitralipi string starts with "CHL1:" (Unicode
 *    14-bit text) or "CHL1a:" (ASCII Base64URL text).
 *  - Pixels are 8-bit, row-major, tightly packed. Channels:
 *      1 = gray, 2 = gray + alpha, 3 = RGB, 4 = RGBA.
 *  - Everything this library returns through an out-pointer must be freed
 *    with chl_free().
 */
#ifndef CHITRALIPI_H
#define CHITRALIPI_H

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32) || defined(__CYGWIN__)
#  if defined(CHITRALIPI_BUILDING)
#    define CHL_API __declspec(dllexport)
#  elif defined(CHITRALIPI_SHARED)
#    define CHL_API __declspec(dllimport)
#  else
#    define CHL_API
#  endif
#elif defined(__GNUC__) || defined(__clang__)
#  define CHL_API __attribute__((visibility("default")))
#else
#  define CHL_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define CHL_VERSION_MAJOR 1
#define CHL_VERSION_MINOR 1
#define CHL_VERSION_PATCH 0

typedef enum chl_status {
    CHL_OK = 0,
    CHL_ERR_ARGUMENT = 1,     /* bad argument (null pointer, bad size, bad option) */
    CHL_ERR_NOMEM = 2,        /* out of memory */
    CHL_ERR_PREFIX = 3,       /* string does not start with CHL1: or CHL1a: */
    CHL_ERR_TEXT = 4,         /* illegal character or bad padding in the text */
    CHL_ERR_FORMAT = 5,       /* broken or truncated package */
    CHL_ERR_CHECKSUM = 6,     /* CRC-32 mismatch: string was changed or cut */
    CHL_ERR_UNSUPPORTED = 7,  /* newer version or unknown feature */
    CHL_ERR_LIMIT = 8,        /* image or data larger than the safety limit */
    CHL_ERR_TOO_BIG = 9,      /* cannot fit inside max_chars */
    CHL_ERR_WRONG_MODE = 10,  /* image string given to unpack, or file string to decode */
    CHL_ERR_INTERNAL = 11
} chl_status;

typedef enum chl_text_mode {
    CHL_TEXT_UNICODE = 0,  /* "CHL1:"  14 bits per character (default, shortest) */
    CHL_TEXT_ASCII = 1     /* "CHL1a:" Base64URL, 6 bits per character */
} chl_text_mode;

/* Fixed layout (8 x int32 then uint64) so every FFI can mirror it. */
typedef struct chl_options {
    int32_t quality;        /* 1..100. 100 = lossless pixels. Default 80. */
    int32_t base_step;      /* > 0 overrides quality with an exact quantizer step. Default 0. */
    int32_t levels;         /* wavelet levels 0..15. Default 5. */
    int32_t chroma_420;     /* -1 auto (on when lossy), 0 off, 1 on. Default -1. */
    int32_t alpha_lossless; /* 1 = keep alpha exact even when lossy. Default 1. */
    int32_t text_mode;      /* chl_text_mode. Default CHL_TEXT_UNICODE. */
    int32_t allow_resize;   /* with max_chars: may shrink the image to fit. Default 1. */
    int32_t reserved;       /* must be 0 */
    uint64_t max_chars;     /* 0 = no limit. Else whole string (prefix included) fits in this many characters. */
} chl_options;

typedef struct chl_info {
    int32_t version;        /* format version, 1 */
    int32_t mode;           /* 0 = packed file (exact bytes), 1 = image codec */
    int32_t channels;       /* codec: channels decode will return */
    int32_t levels;         /* codec: wavelet levels */
    uint32_t width;         /* codec */
    uint32_t height;        /* codec */
    uint32_t base_step;     /* codec: 1 = lossless */
    uint32_t file_kind;     /* packed: 0 unknown 1 JPEG 2 PNG 3 WebP 4 AVIF 5 GIF 6 HEIF */
    uint32_t flags;         /* raw flag byte */
    uint32_t reserved;
    uint64_t data_bytes;    /* payload bytes */
    uint64_t package_bytes; /* whole binary package */
    uint64_t chars;         /* characters in the string */
} chl_info;

CHL_API const char* chl_version(void);
CHL_API const char* chl_status_string(int32_t status);
CHL_API void chl_options_default(chl_options* options);
CHL_API void chl_free(void* pointer);

/* Number of Unicode characters in a UTF-8 string (what chat apps count). */
CHL_API uint64_t chl_char_count(const char* text, size_t text_len);

/* Pixels -> string. out_text is NUL-terminated UTF-8; out_len excludes the NUL. */
CHL_API int32_t chl_encode(const uint8_t* pixels, uint32_t width, uint32_t height, int32_t channels,
                           const chl_options* options, char** out_text, size_t* out_len);

/* String -> pixels. out_channels is 1, 2, 3 or 4 (see top of file). */
CHL_API int32_t chl_decode(const char* text, size_t text_len, uint8_t** out_pixels,
                           uint32_t* out_width, uint32_t* out_height, int32_t* out_channels);

/* Same as above with the raw binary package (.chl file) instead of text. */
CHL_API int32_t chl_encode_binary(const uint8_t* pixels, uint32_t width, uint32_t height, int32_t channels,
                                  const chl_options* options, uint8_t** out_bytes, size_t* out_len);
CHL_API int32_t chl_decode_binary(const uint8_t* bytes, size_t len, uint8_t** out_pixels,
                                  uint32_t* out_width, uint32_t* out_height, int32_t* out_channels);

/* Any file -> string and back, byte-for-byte exact (no size reduction). */
CHL_API int32_t chl_pack(const uint8_t* file_bytes, size_t len, int32_t text_mode,
                         char** out_text, size_t* out_len);
CHL_API int32_t chl_unpack(const char* text, size_t text_len, uint8_t** out_bytes, size_t* out_len,
                           uint32_t* out_file_kind);

/* Binary package <-> text envelope. */
CHL_API int32_t chl_text_encode(const uint8_t* bytes, size_t len, int32_t text_mode,
                                char** out_text, size_t* out_len);
CHL_API int32_t chl_text_decode(const char* text, size_t text_len, uint8_t** out_bytes, size_t* out_len);

/* Read the header of a string without decoding the image. */
CHL_API int32_t chl_get_info(const char* text, size_t text_len, chl_info* out_info);

#ifdef __cplusplus
}
#endif

#endif /* CHITRALIPI_H */
