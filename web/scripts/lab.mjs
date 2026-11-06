// Lab helpers for the local dev stack. Never part of the game.
//   node scripts/lab.mjs dsi <runDir>            a stand-in DSi key signs a hello and a drawing with the
//                                                console's C core (build/web/dsi-act); raw bytes go to Geth --dev
//   node scripts/lab.mjs forge <victim> <tx>     a third key writes a fake mailbox entry claiming another sender
import { execFileSync } from 'node:child_process';
import { existsSync, mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { generatePrivateKey, privateKeyToAccount } from 'viem/accounts';
import { NETWORKS } from '../src/config.js';
import * as arkiv from '../src/arkiv.js';
import { Gift } from '../src/arkiv.js';
import { createWalletClient, ExpirationTime } from '@arkiv-network/sdk';
import { addr, u64 } from '@arkiv-network/sdk/schema';
import { http } from 'viem';

const L = NETWORKS.local;
const rpc = (url, method, params) => fetch(url, { method: 'POST', headers: { 'content-type': 'application/json' }, body: JSON.stringify({ jsonrpc: '2.0', id: 1, method, params }) })
  .then((r) => r.json()).then((j) => { if (j.error) throw new Error(j.error.message); return j.result; });
const fund = async (url, to) => { const [dev] = await rpc(url, 'eth_accounts', []); return rpc(url, 'eth_sendTransaction', [{ from: dev, to, value: '0xde0b6b3a7640000' }]); };
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

const [cmd, ...args] = process.argv.slice(2);
if (cmd === 'dsi') {
  const dir = args[0]; mkdirSync(dir, { recursive: true });
  const keyFile = `${dir}/dsi-standin.key`;
  if (!existsSync(keyFile)) writeFileSync(keyFile, generatePrivateKey() + '\n', { mode: 0o600 });
  const who = privateKeyToAccount(readFileSync(keyFile, 'utf8').trim()).address;
  await fund(L.rpc, who); await sleep(3000);
  const out = { address: who, chainId: L.chainId, acts: [] };
  for (const kind of ['hello', 'drawing']) {
    const nonce = parseInt(await rpc(L.rpc, 'eth_getTransactionCount', [who, 'pending']), 16);
    const raw = execFileSync('../build/web/dsi-act', [keyFile, String(L.chainId), String(nonce), kind]).toString().trim();
    const hash = await rpc(L.rpc, 'eth_sendRawTransaction', ['0x' + raw]);
    out.acts.push({ kind, nonce, hash, rawBytes: raw.length / 2 });
    console.log(kind, hash);
  }
  writeFileSync(`${dir}/dsi-standin.json`, JSON.stringify(out, null, 2) + '\n');
} else if (cmd === 'forge') {
  const [victim, tx] = args;
  const key = generatePrivateKey(), forger = privateKeyToAccount(key);
  await fund(L.arkiv.rpc, forger.address); await sleep(2500);
  const real = await rpc(L.rpc, 'eth_getTransactionByHash', [tx]);
  const sealed = Uint8Array.from(Buffer.from(real.input.slice(2 + 12), 'hex'));
  // Claims the real sender in "from", points at the real tx, but is written by the forger's key.
  const w = createWalletClient({ chain: arkiv.arkivChain(L.arkiv), transport: http(L.arkiv.rpc), account: forger });
  const a = await w.createEntity(Gift, { to: addr(victim), from: addr(real.from), kind: u64(5n), chain: u64(BigInt(L.chainId)), tx, block: u64(BigInt(real.blockNumber)), sealed }, { expiration: ExpirationTime.fromDays(1) });
  // A second one written by the forger as itself, pointing at someone else's transaction.
  const b = await w.createEntity(Gift, { to: addr(victim), from: addr(forger.address), kind: u64(5n), chain: u64(BigInt(L.chainId)), tx, block: u64(BigInt(real.blockNumber)), sealed }, { expiration: ExpirationTime.fromDays(1) });
  console.log(JSON.stringify({ forger: forger.address, claimsSender: a.entityKey, ownName: b.entityKey }, null, 2));
} else {
  console.error('usage: lab.mjs dsi <dir> | forge <victim> <tx>'); process.exit(2);
}
