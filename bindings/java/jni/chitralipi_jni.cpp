// JNI glue for io.github.suvojit20070.chitralipi.Chitralipi (desktop Java and Android).
// SPDX-License-Identifier: MIT
#include <jni.h>

#include <cstring>
#include <vector>

#include "chitralipi/chitralipi.h"

namespace {

void throw_status(JNIEnv* env, int32_t st) {
    jclass cls = env->FindClass("java/lang/IllegalArgumentException");
    if (cls) env->ThrowNew(cls, chl_status_string(st));
}

// Modified UTF-8 from JNI equals real UTF-8 for Chitralipi text (ASCII + BMP, no NUL).
struct Utf8 {
    JNIEnv* env;
    jstring str;
    const char* chars;
    jsize len;
    Utf8(JNIEnv* e, jstring s)
        : env(e), str(s), chars(s ? e->GetStringUTFChars(s, nullptr) : nullptr), len(s ? e->GetStringUTFLength(s) : 0) {}
    ~Utf8() {
        if (chars) env->ReleaseStringUTFChars(str, chars);
    }
};

}  // namespace

extern "C" {

JNIEXPORT jstring JNICALL Java_io_github_suvojit20070_chitralipi_Chitralipi_nativeEncode(
    JNIEnv* env, jclass, jintArray argb, jint width, jint height, jint quality, jlong maxChars, jboolean ascii,
    jboolean allowResize, jint levels) {
    if (!argb || width <= 0 || height <= 0 || env->GetArrayLength(argb) != jlong(width) * height) {
        throw_status(env, CHL_ERR_ARGUMENT);
        return nullptr;
    }
    const size_t count = size_t(width) * size_t(height);
    std::vector<uint8_t> rgba(count * 4);
    jint* px = env->GetIntArrayElements(argb, nullptr);
    if (!px) return nullptr;
    for (size_t i = 0; i < count; ++i) {
        uint32_t c = uint32_t(px[i]);
        rgba[i * 4] = uint8_t(c >> 16);
        rgba[i * 4 + 1] = uint8_t(c >> 8);
        rgba[i * 4 + 2] = uint8_t(c);
        rgba[i * 4 + 3] = uint8_t(c >> 24);
    }
    env->ReleaseIntArrayElements(argb, px, JNI_ABORT);

    chl_options o;
    chl_options_default(&o);
    o.quality = quality;
    o.max_chars = maxChars > 0 ? uint64_t(maxChars) : 0;
    o.text_mode = ascii ? CHL_TEXT_ASCII : CHL_TEXT_UNICODE;
    o.allow_resize = allowResize ? 1 : 0;
    o.levels = levels;
    char* text = nullptr;
    size_t len = 0;
    int32_t st = chl_encode(rgba.data(), uint32_t(width), uint32_t(height), 4, &o, &text, &len);
    if (st != CHL_OK) {
        throw_status(env, st);
        return nullptr;
    }
    jstring result = env->NewStringUTF(text);
    chl_free(text);
    return result;
}

// Returns [width, height, argb...].
JNIEXPORT jintArray JNICALL Java_io_github_suvojit20070_chitralipi_Chitralipi_nativeDecode(JNIEnv* env, jclass,
                                                                                       jstring text) {
    Utf8 s(env, text);
    if (!s.chars) {
        throw_status(env, CHL_ERR_ARGUMENT);
        return nullptr;
    }
    uint8_t* px = nullptr;
    uint32_t w = 0, h = 0;
    int32_t ch = 0;
    int32_t st = chl_decode(s.chars, size_t(s.len), &px, &w, &h, &ch);
    if (st != CHL_OK) {
        throw_status(env, st);
        return nullptr;
    }
    const size_t count = size_t(w) * h;
    if (count + 2 > size_t(0x7FFFFFFF)) {
        chl_free(px);
        throw_status(env, CHL_ERR_LIMIT);
        return nullptr;
    }
    std::vector<jint> out(count + 2);
    out[0] = jint(w);
    out[1] = jint(h);
    for (size_t i = 0; i < count; ++i) {
        const uint8_t* p = px + i * size_t(ch);
        uint32_t r, g, b, a = 255;
        if (ch <= 2) {
            r = g = b = p[0];
            if (ch == 2) a = p[1];
        } else {
            r = p[0];
            g = p[1];
            b = p[2];
            if (ch == 4) a = p[3];
        }
        out[i + 2] = jint((a << 24) | (r << 16) | (g << 8) | b);
    }
    chl_free(px);
    jintArray arr = env->NewIntArray(jsize(out.size()));
    if (arr) env->SetIntArrayRegion(arr, 0, jsize(out.size()), out.data());
    return arr;
}

JNIEXPORT jstring JNICALL Java_io_github_suvojit20070_chitralipi_Chitralipi_nativePack(JNIEnv* env, jclass,
                                                                                    jbyteArray file, jboolean ascii) {
    if (!file) {
        throw_status(env, CHL_ERR_ARGUMENT);
        return nullptr;
    }
    jsize n = env->GetArrayLength(file);
    std::vector<uint8_t> data(static_cast<size_t>(n));
    if (n) env->GetByteArrayRegion(file, 0, n, reinterpret_cast<jbyte*>(data.data()));
    char* text = nullptr;
    size_t len = 0;
    int32_t st = chl_pack(data.data(), data.size(), ascii ? CHL_TEXT_ASCII : CHL_TEXT_UNICODE, &text, &len);
    if (st != CHL_OK) {
        throw_status(env, st);
        return nullptr;
    }
    jstring result = env->NewStringUTF(text);
    chl_free(text);
    return result;
}

JNIEXPORT jbyteArray JNICALL Java_io_github_suvojit20070_chitralipi_Chitralipi_nativeUnpack(JNIEnv* env, jclass,
                                                                                        jstring text) {
    Utf8 s(env, text);
    if (!s.chars) {
        throw_status(env, CHL_ERR_ARGUMENT);
        return nullptr;
    }
    uint8_t* bytes = nullptr;
    size_t n = 0;
    uint32_t kind = 0;
    int32_t st = chl_unpack(s.chars, size_t(s.len), &bytes, &n, &kind);
    if (st != CHL_OK) {
        throw_status(env, st);
        return nullptr;
    }
    if (n > size_t(0x7FFFFFFF)) {
        chl_free(bytes);
        throw_status(env, CHL_ERR_LIMIT);
        return nullptr;
    }
    jbyteArray arr = env->NewByteArray(jsize(n));
    if (arr && n) env->SetByteArrayRegion(arr, 0, jsize(n), reinterpret_cast<const jbyte*>(bytes));
    chl_free(bytes);
    return arr;
}

JNIEXPORT jstring JNICALL Java_io_github_suvojit20070_chitralipi_Chitralipi_nativeVersion(JNIEnv* env, jclass) {
    return env->NewStringUTF(chl_version());
}

}  // extern "C"
