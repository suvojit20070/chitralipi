// node bindings/js/test.mjs   (after copying chitralipi.mjs + chitralipi.wasm next to index.mjs)
// SPDX-License-Identifier: MIT
import { loadChitralipi } from './index.mjs';

const chl = await loadChitralipi();
const w = 48, h = 32;
const px = new Uint8Array(w * h * 3);
for (let i = 0; i < px.length; i++) px[i] = (i * 13) & 255;

const text = chl.encode(px, w, h, 3, { quality: 100 });
const back = chl.decode(text);
if (back.width !== w || back.height !== h || back.channels !== 3) throw new Error('size mismatch');
for (let i = 0; i < px.length; i++) if (px[i] !== back.pixels[i]) throw new Error('pixel mismatch at ' + i);

const small = chl.encode(px, w, h, 3, { maxChars: 200 });
if (chl.charCount(small) > 200) throw new Error('maxChars not respected');

const file = new Uint8Array([0xff, 0xd8, 0xff, 0, 1, 2]);
const again = chl.unpack(chl.pack(file));
if (again.length !== file.length || again.some((v, i) => v !== file[i])) throw new Error('pack mismatch');

console.log(`JS ${chl.version()}: all ok, lossless ${chl.charCount(text)} chars, small ${chl.charCount(small)} chars`);
