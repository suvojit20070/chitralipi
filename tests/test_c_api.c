/* Checks that the public header compiles as plain C and the round trip works. */
/* SPDX-License-Identifier: MIT */
#include "chitralipi/chitralipi.h"

#include <stdio.h>
#include <string.h>

int main(void) {
    unsigned char px[4 * 4 * 3];
    for (int i = 0; i < (int)sizeof(px); ++i) px[i] = (unsigned char)(i * 7);

    chl_options o;
    chl_options_default(&o);
    o.quality = 100;

    char* text = NULL;
    size_t len = 0;
    if (chl_encode(px, 4, 4, 3, &o, &text, &len) != CHL_OK) return 1;
    printf("%s\n", text);

    unsigned char* back = NULL;
    uint32_t w = 0, h = 0;
    int32_t ch = 0;
    int32_t st = chl_decode(text, len, &back, &w, &h, &ch);
    int ok = st == CHL_OK && w == 4 && h == 4 && ch == 3 && memcmp(back, px, sizeof(px)) == 0;
    chl_free(back);
    chl_free(text);
    printf("C API round trip: %s\n", ok ? "ok" : "FAILED");
    return ok ? 0 : 1;
}
