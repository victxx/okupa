// ECIES exactly as the console does it (core/src/ecies.c, the RLPx variant):
// 0x04 || R (64) || iv (16) || AES-128-CTR(m) || HMAC-SHA256 (32).
// kE || kM' = sha256(00000001 || x(ECDH)); MAC key = sha256(kM'); the MAC covers iv || c || auth.
import { secp256k1 } from '@noble/curves/secp256k1.js';
import { sha256 } from '@noble/hashes/sha2.js';
import { hmac } from '@noble/hashes/hmac.js';
import { ctr } from '@noble/ciphers/aes.js';
import { concat, equal } from './bytes.js';

export const OVERHEAD = 65 + 16 + 32;

const point = (pub64) => concat(new Uint8Array([4]), pub64);
export const publicKey = (priv) => secp256k1.getPublicKey(priv, false).subarray(1); // 64 B, no prefix

function derive(priv, pub64) {
  const x = secp256k1.getSharedSecret(priv, point(pub64), true).subarray(1);
  const k = sha256(concat(new Uint8Array([0, 0, 0, 1]), x));
  return { ke: k.subarray(0, 16), km: sha256(k.subarray(16, 32)) };
}

// ephPriv and iv are injectable for test vectors; normally fresh random.
export function encrypt(pub64, msg, auth = new Uint8Array(0), ephPriv, iv) {
  ephPriv ??= secp256k1.utils.randomSecretKey();
  iv ??= crypto.getRandomValues(new Uint8Array(16));
  const { ke, km } = derive(ephPriv, pub64);
  const c = ctr(ke, iv).encrypt(msg);
  const tag = hmac(sha256, km, concat(iv, c, auth));
  return concat(new Uint8Array([4]), publicKey(ephPriv), iv, c, tag);
}

// Checks the MAC first; throws if it does not match.
export function decrypt(priv, data, auth = new Uint8Array(0)) {
  if (data.length < OVERHEAD || data[0] !== 4) throw new Error('ecies: format');
  const m = data.length - OVERHEAD;
  const { ke, km } = derive(priv, data.subarray(1, 65));
  const iv = data.subarray(65, 81), c = data.subarray(81, 81 + m);
  const want = hmac(sha256, km, concat(iv, c, auth));
  if (!equal(want, data.subarray(81 + m))) throw new Error('ecies: mac');
  return ctr(ke, iv).decrypt(c);
}
