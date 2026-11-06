// Swarm through the Bee HTTP API. Pictures are uploaded with Swarm-Encrypt: true, so the
// reference is 64 bytes: chunk address (32) || decryption key (32). Without it the chunks
// are noise. The Bee node is a gateway: it could withhold a picture, but the sealed
// keccak256 lets the recipient reject anything else.
import { bytesToHex, hexToBytes } from './bytes.js';

export async function postageBatch(bee) {
  const r = await fetch(`${bee}/stamps`);
  if (!r.ok) throw new Error(`bee /stamps: ${r.status}`);
  const { stamps } = await r.json();
  const usable = (stamps || []).find((s) => s.usable);
  if (usable) return usable.batchID;
  throw new Error('no usable postage batch on this Bee node');
}

export async function uploadEncrypted(bee, bytes, batch) {
  const r = await fetch(`${bee}/bytes`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/octet-stream', 'Swarm-Encrypt': 'true', 'Swarm-Postage-Batch-Id': batch },
    body: bytes,
  });
  if (!r.ok) throw new Error(`bee upload: ${r.status} ${await r.text()}`);
  const { reference } = await r.json();
  const ref = hexToBytes(reference);
  if (ref.length !== 64) throw new Error('bee did not return an encrypted (64 B) reference');
  return ref;
}

export async function download(bee, ref64) {
  const r = await fetch(`${bee}/bytes/${bytesToHex(ref64)}`);
  if (!r.ok) throw new Error(`bee download: ${r.status}`);
  return new Uint8Array(await r.arrayBuffer());
}
