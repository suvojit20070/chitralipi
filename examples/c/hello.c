/* Encode an image to a Chitralipi string and decode it back.
 * Build: clang hello.c -o hello $(pkg-config --cflags --libs chitralipi)
 * SPDX-License-Identifier: MIT */
#include <chitralipi/chitralipi.h>
#include <stdio.h>

int main(void) {
    /* A 64x48 RGB gradient. In a real app these pixels come from your image loader. */
    enum { W = 64, H = 48 };
    static unsigned char rgb[W * H * 3];
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            unsigned char* p = &rgb[(y * W + x) * 3];
            p[0] = (unsigned char)(x * 4);
            p[1] = (unsigned char)(y * 5);
            p[2] = (unsigned char)(128 + x - y);
        }

    chl_options opt;
    chl_options_default(&opt);
    opt.max_chars = 300; /* the whole string must fit in 300 characters */

    char* text = NULL;
    size_t len = 0;
    int32_t st = chl_encode(rgb, W, H, 3, &opt, &text, &len);
    if (st != CHL_OK) {
        fprintf(stderr, "encode failed: %s\n", chl_status_string(st));
        return 1;
    }
    printf("%llu characters:\n%s\n", (unsigned long long)chl_char_count(text, len), text);

    unsigned char* pixels = NULL;
    uint32_t w = 0, h = 0;
    int32_t channels = 0;
    st = chl_decode(text, len, &pixels, &w, &h, &channels);
    if (st != CHL_OK) {
        fprintf(stderr, "decode failed: %s\n", chl_status_string(st));
        return 1;
    }
    printf("decoded %ux%u with %d channels\n", w, h, channels);

    chl_free(pixels);
    chl_free(text);
    return 0;
}
