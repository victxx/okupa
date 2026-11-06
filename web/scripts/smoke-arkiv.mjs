// Write one gift entry to the local Arkiv dev node and read it back through the same schema.
import { generatePrivateKey, privateKeyToAccount } from 'viem/accounts';
import { NETWORKS } from '../src/config.js';
import * as arkiv from '../src/arkiv.js';
const cfg = NETWORKS.local.arkiv;
const key = generatePrivateKey(), me = privateKeyToAccount(key).address;
const rpc = (method, params) => fetch(cfg.rpc, { method: 'POST', headers: { 'content-type': 'application/json' }, body: JSON.stringify({ jsonrpc: '2.0', id: 1, method, params }) }).then((r) => r.json());
const [dev] = (await rpc('eth_accounts', [])).result;
await rpc('eth_sendTransaction', [{ from: dev, to: me, value: '0xde0b6b3a7640000' }]);
await new Promise((r) => setTimeout(r, 2500));
const to = privateKeyToAccount(generatePrivateKey()).address;
const w = await arkiv.postGift(cfg, key, { to, kind: 5, chainId: 1337, tx: '0x' + 'ab'.repeat(32), block: 7n, sealed: new Uint8Array([1, 2, 3]) });
console.log('created', w);
const got = await arkiv.giftsFor(cfg, to.toLowerCase(), 1337);
console.log(got.map((g) => ({ ...g, block: String(g.block), sealed: Array.from(g.sealed) })), 'me', me);
