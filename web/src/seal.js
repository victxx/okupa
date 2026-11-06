// A Swarm picture reference sealed to one friend (kind 0x05, docs/WEB.md).
// Plaintext: Swarm encrypted reference (address 32 || key 32) || keccak256(picture) (32).
// The MAC also covers the act header and the recipient address, so a sealed blob
// cannot be replayed as another kind or towards someone else.
import { keccak_256 } from '@noble/hashes/sha3.js';
import { concat, equal, hexToBytes } from './bytes.js';
import { encrypt, decrypt } from './ecies.js';
import { header, KIND } from './okupa.js';

const auth = (toAddress) => concat(header(KIND.GIFT_SWARM), hexToBytes(toAddress));

export function seal(recipientPub64, toAddress, swarmRef64, picture) {
  if (swarmRef64.length !== 64) throw new Error('swarm reference must be encrypted (64 B)');
  return encrypt(recipientPub64, concat(swarmRef64, keccak_256(picture)), auth(toAddress));
}

export function open(priv, myAddress, sealed) {
  const p = decrypt(priv, sealed, auth(myAddress));
  if (p.length !== 96) throw new Error('seal: size');
  return { ref: p.subarray(0, 64), pictureHash: p.subarray(64) };
}

export const checkPicture = (picture, pictureHash) => equal(keccak_256(picture), pictureHash);
