// Arkiv as an untrusted mailbox index: one entity per gift, written by the sender's own key.
// It lets a recipient find gifts without scanning the chain. It is never believed: every
// entry is checked against its Ethereum transaction (same sender, same recipient, same
// sealed bytes) before anything is shown. The index can hide a gift; it cannot forge one.
import { createPublicClient, createWalletClient, ExpirationTime } from '@arkiv-network/sdk';
import { eq, and } from '@arkiv-network/sdk/query';
import { addr, bytes, defineSchema, str, u64 } from '@arkiv-network/sdk/schema';
import { http } from 'viem';
import { privateKeyToAccount } from 'viem/accounts';
import { bytesToHex, hexToBytes } from './bytes.js';

export const Gift = defineSchema('okupa-gift-v1', {
  to: addr().indexed(),
  from: addr().indexed(),
  kind: u64().indexed(),      // Okupa act kind (5 = picture on Swarm)
  chain: u64().indexed(),     // Ethereum chain id of the transaction
  tx: str(),                  // transaction hash
  block: u64(),               // block the sender saw it in (a hint; the chain decides)
  sealed: bytes(),            // the same sealed reference the transaction carries
});

export function arkivChain(cfg) {
  return {
    id: cfg.chainId, name: cfg.name, testnet: true,
    nativeCurrency: { name: cfg.symbol, symbol: cfg.symbol, decimals: 18 },
    rpcUrls: { default: { http: [cfg.rpc] } },
  };
}

export async function postGift(cfg, privHex, g) {
  const chain = arkivChain(cfg);
  const account = privateKeyToAccount(privHex);
  const wallet = createWalletClient({ chain, transport: http(cfg.rpc), account });
  const { entityKey, txHash } = await wallet.createEntity(Gift, {
    to: addr(g.to), from: addr(account.address), kind: u64(BigInt(g.kind)), chain: u64(BigInt(g.chainId)),
    tx: g.tx, block: u64(BigInt(g.block)), sealed: g.sealed,
  }, { expiration: ExpirationTime.fromDays(cfg.days ?? 7) });
  return { entityKey, txHash };
}

export async function giftsFor(cfg, me, chainId) {
  const pub = createPublicClient({ chain: arkivChain(cfg), transport: http(cfg.rpc) });
  const page = await pub.select().from(Gift)
    .where(and(eq('to', addr(me)), eq('chain', u64(BigInt(chainId)))))
    .limit(50).fetch();
  return page.entities.map((e) => ({
    key: e.$key, owner: String(e.$owner).toLowerCase(),
    to: String(e.to.value).toLowerCase(), from: String(e.from.value).toLowerCase(),
    kind: Number(e.kind.value), chainId: Number(e.chain.value), tx: e.tx.value,
    block: BigInt(e.block.value), sealed: toBytes(e.sealed.value),
  }));
}

const toBytes = (v) => (v instanceof Uint8Array ? v : hexToBytes(String(v)));
export const hex = (b) => '0x' + bytesToHex(b);
