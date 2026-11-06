// Renders the console's top screen through scene.wasm and writes a PPM: proof the web uses the game's own renderer.
import { readFileSync, writeFileSync } from 'node:fs';
const { instance } = await WebAssembly.instantiate(readFileSync(new URL('../public/scene.wasm', import.meta.url)), {});
const x = instance.exports;
x.scene_init(0x6f6b7570, 0, 0, 0, 0);
const pic = new Uint8Array(x.memory.buffer, x.scene_pic(), 512);
for (let i = 0; i < 512; i++) pic[i] = (i >> 3) & 1 ? 0xff : 0x00; // stripes
x.scene_hang(0, 0);
for (let t = 0; t < 120; t++) x.scene_step(0);
const ptr = x.scene_draw(10, 30, 0);
const px = new Uint8Array(x.memory.buffer, ptr, 256 * 192 * 4);
const out = [Buffer.from('P6\n256 192\n255\n')];
const rgb = Buffer.alloc(256 * 192 * 3);
let colours = new Set();
for (let i = 0; i < 256 * 192; i++) { rgb[3 * i] = px[4 * i]; rgb[3 * i + 1] = px[4 * i + 1]; rgb[3 * i + 2] = px[4 * i + 2]; colours.add(px[4 * i] << 16 | px[4 * i + 1] << 8 | px[4 * i + 2]); }
out.push(rgb);
writeFileSync(process.argv[2] || '/tmp/scene.ppm', Buffer.concat(out));
console.log('distinct colours', colours.size);
