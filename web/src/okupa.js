// Okupa calldata, the same family the DSi writes (core/include/okupa_act.h, docs/PLAN.md):
// "OKPA" | version 1 | kind | payload.
import { concat, equal } from './bytes.js';

export const MAGIC = new Uint8Array([0x4f, 0x4b, 0x50, 0x41]); // "OKPA"
export const VERSION = 1;
export const KIND = {
  HELLO: 0x00,          // to self, no payload
  DRAWING: 0x01,        // to self, 64x64 1 bpp (512 B)
  PHOTO: 0x02,          // to self, 64x64 2 bpp (1024 B)
  GIFT_PICTURE: 0x03,   // to a friend, drawing or photo inline
  GIFT_OBJECT: 0x04,    // to a friend, 0G root + mesh keccak (planned)
  GIFT_SWARM: 0x05,     // to a friend, picture on Swarm: ECIES-sealed reference (docs/WEB.md)
};
export const SIDE = 64;
export const DRAWING_BYTES = (SIDE * SIDE) / 8;   // 1 bpp, MSB first, row-major
export const PHOTO_BYTES = (SIDE * SIDE) / 4;     // 2 bpp, four tones, MSB first, row-major
export const SEALED_REF_BYTES = 113 + 64 + 32;    // ECIES(reference 64 || keccak(picture) 32)

export const header = (kind) => concat(MAGIC, new Uint8Array([VERSION, kind]));

export const hello = () => header(KIND.HELLO);
export const drawing = (px) => { need(px, DRAWING_BYTES); return concat(header(KIND.DRAWING), px); };
export const photo = (px) => { need(px, PHOTO_BYTES); return concat(header(KIND.PHOTO), px); };
export const giftPicture = (px) => {
  if (px.length !== DRAWING_BYTES && px.length !== PHOTO_BYTES) throw new Error('picture size');
  return concat(header(KIND.GIFT_PICTURE), px);
};
export const giftSwarm = (sealed) => { need(sealed, SEALED_REF_BYTES); return concat(header(KIND.GIFT_SWARM), sealed); };

// Exactly one known shape, or null (like ok_act_decode).
export function decode(data) {
  if (data.length < 6 || !equal(data.subarray(0, 4), MAGIC) || data[4] !== VERSION) return null;
  const kind = data[5], body = data.subarray(6);
  switch (kind) {
    case KIND.HELLO: return body.length === 0 ? { kind } : null;
    case KIND.DRAWING: return body.length === DRAWING_BYTES ? { kind, pixels: body, bpp: 1 } : null;
    case KIND.PHOTO: return body.length === PHOTO_BYTES ? { kind, pixels: body, bpp: 2 } : null;
    case KIND.GIFT_PICTURE:
      if (body.length === DRAWING_BYTES) return { kind, pixels: body, bpp: 1 };
      if (body.length === PHOTO_BYTES) return { kind, pixels: body, bpp: 2 };
      return null;
    case KIND.GIFT_SWARM: return body.length === SEALED_REF_BYTES ? { kind, sealed: body } : null;
    default: return null;
  }
}

function need(b, n) { if (!(b instanceof Uint8Array) || b.length !== n) throw new Error(`expected ${n} bytes`); }

// Pixel access. 1 bpp: 1 = ink. 2 bpp: tone 0..3 (0 = paper, 3 = darkest ink).
export function getPixel(px, bpp, x, y) {
  const i = y * SIDE + x;
  if (bpp === 1) return (px[i >> 3] >> (7 - (i & 7))) & 1;
  return (px[i >> 2] >> (6 - 2 * (i & 3))) & 3;
}
export function setPixel(px, bpp, x, y, v) {
  const i = y * SIDE + x;
  if (bpp === 1) {
    const m = 1 << (7 - (i & 7));
    px[i >> 3] = v ? px[i >> 3] | m : px[i >> 3] & ~m;
  } else {
    const s = 6 - 2 * (i & 3);
    px[i >> 2] = (px[i >> 2] & ~(3 << s)) | ((v & 3) << s);
  }
}
// Four tones down to the DSi's 1 bpp: tones 2 and 3 become ink.
export function toDrawing(photoPx) {
  const out = new Uint8Array(DRAWING_BYTES);
  for (let y = 0; y < SIDE; y++) for (let x = 0; x < SIDE; x++) setPixel(out, 1, x, y, getPixel(photoPx, 2, x, y) >= 2);
  return out;
}
