// The browser seal must match the console byte for byte: same vectors as tests/test_ecies.c.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { hexToBytes, bytesToHex } from '../src/bytes.js';
import * as ecies from '../src/ecies.js';
import { seal, open, checkPicture } from '../src/seal.js';
import * as okupa from '../src/okupa.js';

const V = JSON.parse(readFileSync(new URL('./vectors.json', import.meta.url)));
const enc = new TextEncoder();

test('ecies: same bytes as the independent Python encoder', () => {
  const pubB = ecies.publicKey(hexToBytes(V.KEY_B));
  const iv = Uint8Array.from({ length: 16 }, (_, i) => 0xa0 + i);
  const out = ecies.encrypt(pubB, enc.encode('okupa says hello, through rlpx ecies'), new Uint8Array([0x00, 0x99]), hexToBytes(V.EPH_A), iv);
  assert.equal(bytesToHex(out), V.PY_OUT);
});

test('ecies: opens the EIP-8 handshake vectors', () => {
  const kb = hexToBytes(V.KEY_B), ka = hexToBytes(V.KEY_A);
  assert.equal(ecies.decrypt(kb, hexToBytes(V.AUTH1)).length, hexToBytes(V.AUTH1).length - ecies.OVERHEAD);
  const a2 = hexToBytes(V.AUTH2);
  assert.ok(ecies.decrypt(kb, a2.subarray(2), a2.subarray(0, 2)).length > 0);
  assert.ok(ecies.decrypt(ka, hexToBytes(V.ACK1)).length > 0);
  assert.throws(() => ecies.decrypt(ka, hexToBytes(V.AUTH1)), /mac/);
});

test('seal: only the named recipient opens it, and it binds the address', () => {
  const priv = hexToBytes(V.KEY_A), pub = ecies.publicKey(priv);
  const addr = '0x' + '11'.repeat(20), other = '0x' + '22'.repeat(20);
  const ref = Uint8Array.from({ length: 64 }, (_, i) => i);
  const pic = okupa.photo(new Uint8Array(okupa.PHOTO_BYTES).fill(0x1b));
  const s = seal(pub, addr, ref, pic);
  assert.equal(s.length, okupa.SEALED_REF_BYTES);
  const o = open(priv, addr, s);
  assert.deepEqual(o.ref, ref);
  assert.ok(checkPicture(pic, o.pictureHash));
  assert.ok(!checkPicture(pic.slice(0, -1), o.pictureHash));
  assert.throws(() => open(priv, other, s), /mac/);
  assert.throws(() => open(hexToBytes(V.KEY_B), addr, s), /mac/);
});

test('calldata: same family as the DSi', () => {
  assert.equal(bytesToHex(okupa.hello()), '4f4b50410100');
  const px = new Uint8Array(okupa.DRAWING_BYTES); okupa.setPixel(px, 1, 3, 0, 1);
  assert.equal(px[0], 0x10);
  const d = okupa.decode(okupa.drawing(px));
  assert.equal(d.kind, okupa.KIND.DRAWING);
  assert.equal(okupa.decode(okupa.giftPicture(px)).bpp, 1);
  const ph = new Uint8Array(okupa.PHOTO_BYTES); okupa.setPixel(ph, 2, 1, 0, 3);
  assert.equal(ph[0], 0x30);
  assert.equal(okupa.getPixel(okupa.toDrawing(ph), 1, 1, 0), 1);
  assert.equal(okupa.decode(okupa.giftSwarm(new Uint8Array(okupa.SEALED_REF_BYTES))).kind, 5);
  assert.equal(okupa.decode(new Uint8Array([0x4f, 0x4b, 0x50, 0x41, 1, 1, 0])), null);
  assert.equal(okupa.decode(new Uint8Array([0x4f, 0x4b, 0x50, 0x41, 1, 9])), null);
});
