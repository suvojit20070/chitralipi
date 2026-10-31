# Chitralipi

**Image do, string lo. String do, image lo.**

Chitralipi (चित्रलिपि, "picture writing") turns an image into a short text string and back, with no server and no third-party libraries. It has its own wavelet image codec and its own text alphabet, so a photo fits in a chat message, a JSON field, a QR code or a database column.

```text
CHL1:廓伐帐丅乊蚯薧腀莴碓啤胏梳嫟囬莳拯榙綔屿濴导燁楫恫歛衷嶡诛貇礦殗...
```

- **Small strings.** Every character carries 14 bits (Base64 carries 6), and the codec matches or beats JPEG at the same size.
- **Fit to a limit.** Ask for `max_chars = 4000` and Chitralipi picks the quality (and size, if needed) that fits.
- **Lossless when you want.** `quality = 100` gives back the exact pixels. `pack` keeps any file byte-for-byte.
- **Everywhere.** One C++17 core with a plain C API, built and tested in CI on Linux, Windows, macOS, Android and WebAssembly (and portable to anything with a C++17 compiler). Bindings for Java/Android, Python and JavaScript.
- **Safe on bad input.** CRC-32 on every string, strict size limits, and corrupted text is rejected instead of crashing.

## How small?

Measured with this code on standard test photos. "JPEG at same size" is the best JPEG (Pillow) that fits in the same number of bytes.

| Photo | Setting | Characters | Quality (PSNR) | JPEG at same size |
| --- | --- | --- | --- | --- |
| Astronaut 512×512 | quality 80 | 52,126 | 38.5 dB | 37.9 dB |
| Astronaut 512×512 | quality 40 | 8,876 | 31.3 dB | 28.8 dB |
| Coffee 600×400 | quality 80 | 56,865 | 38.0 dB | 37.0 dB |
| Cat 451×300 | quality 40 | 3,428 | 32.2 dB | 29.1 dB |
| Cat 451×300 | max_chars 4096 | 3,956 | 32.8 dB | |
| 12 MP photo | max_chars 20,000 | 19,843 | stored at 1116×837, 0.8 s | |

Higher PSNR is better; around 35 dB and above, differences are usually hard to spot.

**Screenshots and UI** need more characters than photos, because sharp text is expensive for every codec. A 1080×641 app screenshot: 4,000 characters gives 26.6 dB (text blurry), 10,000 gives 32.5 dB (text readable), 20,000 gives 38.4 dB (close to the original). Here WebP does better at the same size (37.7 dB at 10,000 characters' worth of bytes); improving screenshots is on the roadmap.

**What it cannot do:** no format can store any 10 MB photo as a few KB *and* give back the identical file. Lossless strings are about PNG size. Small strings come from lossy coding, which looks the same to the eye but is not bit-identical. See [SPEC.md](SPEC.md) for the details.

## Install

**Termux (Android) or Linux**, one command (builds from source, runs the tests, installs the CLI, library, header, pkg-config file, CMake package and Python module):

```sh
curl -fsSL https://raw.githubusercontent.com/suvojit20070/chitralipi/main/scripts/install.sh | bash
```

Full developer guide for Termux: [docs/TERMUX.md](docs/TERMUX.md).

**Prebuilt files** for Windows, macOS, Linux, Android (`.aar`), Java (`.jar`) and WebAssembly are on the [Releases page](https://github.com/suvojit20070/chitralipi/releases).

## Quick start

### Command line

```sh
chitralipi encode photo.jpg -o photo.txt --max-chars 4000
chitralipi decode photo.txt -o back.png
chitralipi pack anyfile.pdf -o file.txt       # exact bytes, any file type
chitralipi unpack file.txt -o anyfile.pdf
chitralipi info photo.txt
```

Options: `-q 1-100` quality (default 80), `--lossless`, `--max-chars N`, `--no-resize`, `--ascii` (only `A-Z a-z 0-9 - _`), `--levels 0-15`.

### C / C++

```c
#include <chitralipi/chitralipi.h>

chl_options opt;
chl_options_default(&opt);
opt.max_chars = 4000;

char* text; size_t len;
if (chl_encode(rgb, width, height, 3, &opt, &text, &len) == CHL_OK) {
    /* use text */
    chl_free(text);
}

uint8_t* px; uint32_t w, h; int32_t channels;
chl_decode(text, len, &px, &w, &h, &channels);
chl_free(px);
```

### Java / Android

Add `chitralipi-android.aar` from the release page (or `chitralipi.jar` + the native library on desktop).

```java
int[] px = new int[w * h];
bitmap.getPixels(px, 0, w, 0, 0, w, h);
String text = Chitralipi.encode(px, w, h, new Chitralipi.Options().maxChars(4000));

Chitralipi.Image img = Chitralipi.decode(text);
Bitmap back = Bitmap.createBitmap(img.argb, img.width, img.height, Bitmap.Config.ARGB_8888);
```

### Python

Copy `bindings/python/chitralipi.py` next to the shared library (or set `CHITRALIPI_LIB`).

```python
import chitralipi
from PIL import Image

text = chitralipi.encode_image(Image.open("photo.jpg"), max_chars=4000)
chitralipi.decode_image(text).save("back.png")
```

### JavaScript (browser or Node)

```js
import { loadChitralipi } from './index.mjs';
const chl = await loadChitralipi();
const text = chl.encode(imageData.data, imageData.width, imageData.height, 4, { maxChars: 4000 });
const img = chl.decode(text);
ctx.putImageData(new ImageData(chl.toRGBA(img), img.width, img.height), 0, 0);
```

### Using the installed library

```sh
clang app.c -o app $(pkg-config --cflags --libs chitralipi)       # C, with pkg-config
```

```cmake
find_package(chitralipi 1 REQUIRED)                               # CMake projects
target_link_libraries(myapp PRIVATE chitralipi::chitralipi)
```

### Other languages

Anything that can call C works: C# (`DllImport`), Go (cgo), Rust (FFI), Dart (`dart:ffi`), Swift, Kotlin/Native, Node (`ffi-napi`). The whole API is in [`include/chitralipi/chitralipi.h`](include/chitralipi/chitralipi.h). Strings are UTF-8 and every buffer the library returns is freed with `chl_free`.

## Build

```sh
cmake -S . -B build -DCHITRALIPI_FETCH_STB=ON     # stb only for the CLI's PNG/JPEG support
cmake --build build --config Release
ctest --test-dir build -C Release
```

| CMake option | Default | What it does |
| --- | --- | --- |
| `CHITRALIPI_FETCH_STB` | OFF | Download stb_image so the CLI reads PNG/JPEG/BMP/GIF and writes PNG/JPEG. Without it the CLI uses PPM/PGM/PAM. The library never needs it. |
| `CHITRALIPI_JNI` | OFF (ON for Android) | Put the Java glue into the shared library. |
| `CHITRALIPI_BUILD_CLI` | ON | Build the `chitralipi` tool. |
| `CHITRALIPI_BUILD_TESTS` | ON | Build the test suite. |

Android (one ABI): `cmake -B build-android -DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-21`

WebAssembly: `emcmake cmake -B build-wasm && cmake --build build-wasm` gives `chitralipi.mjs` + `chitralipi.wasm`.

GitHub Actions (`.github/workflows/build.yml`) builds and tests Linux x64/arm64, macOS, Windows, Android (4 ABIs + AAR), Java and WebAssembly on every push. Push a tag like `v1.0.0` and it attaches everything to a GitHub Release.

## How it works

1. RGB → YCoCg-R (reversible color transform)
2. Optional 4:2:0 color subsampling
3. LeGall 5/3 integer wavelet, 5 levels
4. Quantization (the quality knob)
5. Context-adaptive arithmetic coding (a binary range coder with neighbour and parent contexts)
6. Binary package with header and CRC-32
7. Lipi-14 text: each 14 bits become one character from U+4E00 to U+8DFF

All steps use integer math only, so every platform and language decodes exactly the same pixels. The full byte-level format is in [SPEC.md](SPEC.md).

## License

MIT. See [LICENSE](LICENSE). Copyright (c) 2026 Suvo.
