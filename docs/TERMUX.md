# Using Chitralipi in Termux

Termux gives you a real Linux toolchain on Android, so Chitralipi installs and works there like on any Linux machine. This guide shows how a developer installs it once and then uses it from the shell, C, C++, Python and Node.js.

## 1. Install (one command)

```sh
curl -fsSL https://raw.githubusercontent.com/suvojit20070/chitralipi/main/scripts/install.sh | bash
```

It installs build tools with `pkg`, builds from source, runs the tests and installs into `$PREFIX`:

| What | Where |
| --- | --- |
| `chitralipi` command | `$PREFIX/bin` |
| Library `libchitralipi.so` | `$PREFIX/lib` |
| Header `chitralipi/chitralipi.h` | `$PREFIX/include` |
| pkg-config file | `$PREFIX/lib/pkgconfig/chitralipi.pc` |
| CMake package | `$PREFIX/lib/cmake/chitralipi` |
| Python module | Termux Python `site-packages` |

A specific release: `CHITRALIPI_REF=v1.0.0` before `bash`. Takes 1 to 3 minutes on a phone.

Manual install, if you prefer:

```sh
pkg install clang cmake git make pkg-config
git clone https://github.com/suvojit20070/chitralipi.git && cd chitralipi
cmake -B build -DCMAKE_INSTALL_PREFIX=$PREFIX -DCHITRALIPI_FETCH_STB=ON
cmake --build build -j4 && ctest --test-dir build && cmake --install build
```

To reach your phone's photos, run `termux-setup-storage` once. The camera folder is then `~/storage/dcim/Camera`.

## 2. Shell

```sh
chitralipi encode ~/storage/dcim/Camera/IMG_0001.jpg -o photo.txt --max-chars 4000
chitralipi decode photo.txt -o ~/storage/downloads/back.png
chitralipi info photo.txt
```

With the **Termux:API** app and `pkg install termux-api`, the string can go straight to the clipboard:

```sh
chitralipi encode photo.jpg --max-chars 4000 | termux-clipboard-set   # paste in any chat app
termux-clipboard-get > got.txt && chitralipi decode got.txt -o got.png
```

A small script that turns every photo in a folder into text files:

```sh
for f in ~/storage/dcim/Camera/*.jpg; do
  chitralipi encode "$f" -o "${f##*/}.txt" --max-chars 8000
done
```

## 3. C

```c
#include <chitralipi/chitralipi.h>
#include <stdio.h>

int main(void) {
    static unsigned char rgb[64 * 48 * 3];          /* your pixels */
    chl_options opt;
    chl_options_default(&opt);
    opt.max_chars = 300;

    char* text; size_t len;
    if (chl_encode(rgb, 64, 48, 3, &opt, &text, &len) != CHL_OK) return 1;
    printf("%s\n", text);

    unsigned char* px; uint32_t w, h; int32_t ch;
    chl_decode(text, len, &px, &w, &h, &ch);
    chl_free(px);
    chl_free(text);
    return 0;
}
```

```sh
clang app.c -o app $(pkg-config --cflags --libs chitralipi)
./app
```

A complete example is in [`examples/c/hello.c`](../examples/c/hello.c).

## 4. C++ with CMake

```cmake
cmake_minimum_required(VERSION 3.16)
project(myapp CXX)
find_package(chitralipi 1 REQUIRED)
add_executable(myapp main.cpp)
target_link_libraries(myapp PRIVATE chitralipi::chitralipi)   # or chitralipi::chitralipi_static
```

```sh
cmake -B build && cmake --build build && ./build/myapp
```

Inside C++ the API is the same C functions; wrap the returned pointers in `std::unique_ptr<char, decltype(&chl_free)>` if you like.

## 5. Python

```sh
pkg install python python-pillow
```

```python
import chitralipi
from PIL import Image

text = chitralipi.encode_image(Image.open("photo.jpg"), max_chars=4000)
print(chitralipi.char_count(text), "characters")
chitralipi.decode_image(text).save("back.png")

# Raw pixels without Pillow
text = chitralipi.encode(rgb_bytes, width, height, 3, quality=80)
pixels, width, height, channels = chitralipi.decode(text)

# Any file, exact bytes
s = chitralipi.pack(open("notes.pdf", "rb").read())
open("copy.pdf", "wb").write(chitralipi.unpack(s))
```

A runnable script: [`examples/python/photo.py`](../examples/python/photo.py) (`python photo.py IMG.jpg 4000`).

Errors raise `chitralipi.ChitralipiError` with `.status` and a readable message.

## 6. Node.js (WebAssembly)

No compiler needed: download the WebAssembly build from the release.

```sh
pkg install nodejs unzip
curl -LO https://github.com/suvojit20070/chitralipi/releases/latest/download/chitralipi-wasm.zip
unzip chitralipi-wasm.zip -d chitralipi-js
```

```js
// app.mjs  (next to the chitralipi-js folder)
import { loadChitralipi } from './chitralipi-js/index.mjs';
const chl = await loadChitralipi();

const w = 32, h = 32, rgba = new Uint8Array(w * h * 4).fill(200);
const text = chl.encode(rgba, w, h, 4, { maxChars: 300 });
console.log(text);
const img = chl.decode(text);       // { pixels, width, height, channels }
```

```sh
node app.mjs
```

## 7. Android apps (not Termux)

For an Android Studio / Sketchware / AIDE app, use `chitralipi-android.aar` from the release page and the Java API in [`Chitralipi.java`](../bindings/java/src/main/java/io/github/suvojit20070/chitralipi/Chitralipi.java). The Termux library is built for Termux's own environment; apps should use the AAR.

## Troubleshooting

| Problem | Fix |
| --- | --- |
| `pkg-config: command not found` | `pkg install pkg-config` |
| Python: `Chitralipi library not found` | Set `export CHITRALIPI_LIB=$PREFIX/lib/libchitralipi.so` |
| `Permission denied` on `~/storage` | Run `termux-setup-storage` and allow the permission |
| Build runs out of memory | Use `cmake --build build -j2` |
| `CHL_ERR_CHECKSUM` when decoding | The text was cut or changed while copying; copy the whole string |
