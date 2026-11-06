// Optional private reads for visits through anon-rpc (https://ethereum.github.io/anon-rpc/):
// a hash-pinned client, named by an on-chain specifier, runs in a null-origin sandboxed
// worker and hands back a fetch. Specifier addresses from the anon-rpc adopters page.
// Reading the specifier itself is one plain eth_call to the chain it lives on.
import { AnonRpcWorker } from '@anon-rpc/browser-harness';
import { NETWORKS } from './config.js';

export const ANON = {
  passthrough: { name: 'anon-rpc passthrough (sandbox only, not anonymous)', address: '0x4fd77be300f31c5fe6ab266d35d27750a3478d27', chain: 'mainnet' },
  tor: { name: 'anon-rpc over Tor (tor-js)', address: '0x700dA3193D35fA54Cd3fBf29B66f2a2A0385659e', chain: 'mainnet',
    config: { gateways: ['152.236.3.22:12298:uEiCH0VdbhryzJcMtCfB0vLA4sOvHdQ4hq5w7CopcwU59bQ'] } },
  nox: { name: 'anon-rpc over Nox mixnet (Sepolia specifier)', address: '0x35133267A8F7EC35046a83DCA0f4Ca8A6e1b7A9A', chain: 'sepolia' },
};

const live = new Map();
export function anonFetch(id) {
  if (!id || !ANON[id]) return null;
  if (!live.has(id)) {
    const a = ANON[id], rpc = NETWORKS[a.chain].rpc;
    const rpcProvider = {
      request: async ({ method, params }) => {
        const r = await fetch(rpc, { method: 'POST', headers: { 'content-type': 'application/json' }, body: JSON.stringify({ jsonrpc: '2.0', id: 1, method, params: params ?? [] }) });
        const j = await r.json();
        if (j.error) throw new Error(j.error.message);
        return j.result;
      },
    };
    const w = new AnonRpcWorker({ address: a.address, config: a.config, preExisting: { rpcProvider } });
    live.set(id, w);
  }
  const w = live.get(id);
  return { fetch: w.fetch, ready: w.ready, name: ANON[id].name };
}
