// Chitralipi for JavaScript (browser, Node, Deno, Bun) through WebAssembly.
// Build chitralipi.mjs + chitralipi.wasm with:  emcmake cmake -B build-wasm && cmake --build build-wasm
// SPDX-License-Identifier: MIT
//
//   import { loadChitralipi } from './index.mjs';
//   const chl = await loadChitralipi();
//   const text = chl.encode(imageData.data, imageData.width, imageData.height, 4, { maxChars: 4000 });
//   const img = chl.decode(text);              // { pixels, width, height, channels }
//   ctx.putImageData(new ImageData(chl.toRGBA(img), img.width, img.height), 0, 0);

import createChitralipi from './chitralipi.mjs';

const encoder = new TextEncoder();
const decoder = new TextDecoder();

export async function loadChitralipi(moduleOptions = {}) {
  return new Chitralipi(await createChitralipi(moduleOptions));
}

export class Chitralipi {
  constructor(module) {
    this.M = module;
  }

  version() {
    return this.M.UTF8ToString(this.M._chl_version());
  }

  #check(status) {
    if (status !== 0) throw new Error('Chitralipi: ' + this.M.UTF8ToString(this.M._chl_status_string(status)));
  }

  #copyIn(bytes) {
    const p = this.M._malloc(Math.max(1, bytes.length));
    this.M.HEAPU8.set(bytes, p);
    return p;
  }

  #u32(ptr) {
    return this.M.getValue(ptr, 'i32') >>> 0;
  }

  // pixels: Uint8Array / Uint8ClampedArray, row-major, `channels` per pixel (1-4).
  encode(pixels, width, height, channels = 4,
         { quality = 80, maxChars = 0, ascii = false, allowResize = true, levels = 5 } = {}) {
    const M = this.M;
    if (pixels.length !== width * height * channels) throw new Error('pixel buffer size mismatch');
    const px = this.#copyIn(pixels);
    const opt = M._malloc(40);
    const out = M._malloc(8);
    try {
      M._chl_options_default(opt);
      M.setValue(opt, quality, 'i32');
      M.setValue(opt + 8, levels, 'i32');
      M.setValue(opt + 20, ascii ? 1 : 0, 'i32');
      M.setValue(opt + 24, allowResize ? 1 : 0, 'i32');
      M.setValue(opt + 32, maxChars % 4294967296, 'i32');
      M.setValue(opt + 36, Math.floor(maxChars / 4294967296), 'i32');
      this.#check(M._chl_encode(px, width, height, channels, opt, out, out + 4));
      const textPtr = this.#u32(out), len = this.#u32(out + 4);
      const text = decoder.decode(M.HEAPU8.slice(textPtr, textPtr + len));
      M._chl_free(textPtr);
      return text;
    } finally {
      M._free(px);
      M._free(opt);
      M._free(out);
    }
  }

  // Returns { pixels: Uint8Array, width, height, channels }.
  decode(text) {
    const M = this.M;
    const bytes = encoder.encode(text);
    const p = this.#copyIn(bytes);
    const out = M._malloc(16);
    try {
      this.#check(M._chl_decode(p, bytes.length, out, out + 4, out + 8, out + 12));
      const ptr = this.#u32(out), width = this.#u32(out + 4), height = this.#u32(out + 8);
      const channels = M.getValue(out + 12, 'i32');
      const pixels = M.HEAPU8.slice(ptr, ptr + width * height * channels);
      M._chl_free(ptr);
      return { pixels, width, height, channels };
    } finally {
      M._free(p);
      M._free(out);
    }
  }

  // Any file (Uint8Array) -> string, byte-for-byte exact.
  pack(fileBytes, ascii = false) {
    const M = this.M;
    const p = this.#copyIn(fileBytes);
    const out = M._malloc(8);
    try {
      this.#check(M._chl_pack(p, fileBytes.length, ascii ? 1 : 0, out, out + 4));
      const textPtr = this.#u32(out), len = this.#u32(out + 4);
      const text = decoder.decode(M.HEAPU8.slice(textPtr, textPtr + len));
      M._chl_free(textPtr);
      return text;
    } finally {
      M._free(p);
      M._free(out);
    }
  }

  unpack(text) {
    const M = this.M;
    const bytes = encoder.encode(text);
    const p = this.#copyIn(bytes);
    const out = M._malloc(12);
    try {
      this.#check(M._chl_unpack(p, bytes.length, out, out + 4, out + 8));
      const ptr = this.#u32(out), len = this.#u32(out + 4);
      const data = M.HEAPU8.slice(ptr, ptr + len);
      M._chl_free(ptr);
      return data;
    } finally {
      M._free(p);
      M._free(out);
    }
  }

  // Any decoded image -> RGBA Uint8ClampedArray (for canvas ImageData).
  toRGBA({ pixels, width, height, channels }) {
    if (channels === 4) return new Uint8ClampedArray(pixels.buffer, pixels.byteOffset, pixels.length);
    const out = new Uint8ClampedArray(width * height * 4);
    for (let i = 0, j = 0; i < width * height; i++, j += channels) {
      const gray = channels <= 2;
      out[i * 4] = pixels[j];
      out[i * 4 + 1] = gray ? pixels[j] : pixels[j + 1];
      out[i * 4 + 2] = gray ? pixels[j] : pixels[j + 2];
      out[i * 4 + 3] = channels === 2 ? pixels[j + 1] : 255;
    }
    return out;
  }

  // Characters as chat apps count them.
  charCount(text) {
    return [...text].length;
  }
}
