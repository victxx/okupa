// Reads one monkey's Arkiv mailbox from outside the browser: same schema and the same
// chain checks as the page, minus the seal, which only the recipient's key opens.
//   node scripts/read-mailbox.mjs <local|sepolia> <address>
import { NETWORKS } from '../src/config.js';
import * as arkiv from '../src/arkiv.js';
import { verifiedAct } from '../src/chain.js';
import { equal } from '../src/bytes.js';

const [netId, who] = process.argv.slice(2);
const n = NETWORKS[netId], me = who.toLowerCase();
const out = [];
for (const e of await arkiv.giftsFor(n.arkiv, me, n.chainId)) {
  const v = await verifiedAct(n, e.tx);
  let why = null;
  if (!v.ok) why = v.why;
  else if (e.owner !== e.from) why = 'entry not written by the sender it names';
  else if (v.from !== e.from) why = 'transaction sender differs from the entry';
  else if (v.to !== me) why = 'transaction not addressed to this mailbox';
  else if (v.act.kind !== e.kind) why = 'kind differs';
  else if (v.act.sealed && !equal(v.act.sealed, e.sealed)) why = 'sealed bytes differ';
  out.push({ entity: e.key, owner: e.owner, from: e.from, kind: e.kind, tx: e.tx, block: String(e.block),
    sealedBytes: e.sealed.length, chainCheck: why ? `reject: ${why}` : 'pass' });
}
console.log(JSON.stringify({ network: netId, mailbox: me, arkivRpc: n.arkiv.rpc, entries: out }, null, 2));
process.exit(0);
