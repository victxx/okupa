// Ethereum for the web monkey. A browser cannot open TCP, so unlike the DSi it reads
// and sends through a JSON-RPC endpoint. Everything it is told is checked against
// the transaction itself: sender (recovered from the signature), recipient and calldata.
import { createPublicClient, createWalletClient, http, recoverTransactionAddress, serializeTransaction, keccak256 } from 'viem';
import { privateKeyToAccount } from 'viem/accounts';
import { bytesToHex, hexToBytes, equal } from './bytes.js';
import { decode } from './okupa.js';

export function chainOf(net) {
  return {
    id: net.chainId, name: net.name,
    nativeCurrency: { name: 'Ether', symbol: 'ETH', decimals: 18 },
    rpcUrls: { default: { http: [net.rpc] } },
  };
}

export function clients(net, privHex, fetchFn) {
  const chain = chainOf(net);
  const pub = createPublicClient({ chain, transport: http(net.rpc, fetchFn ? { fetchFn } : {}) });
  const wallet = privHex ? createWalletClient({ chain, transport: http(net.rpc), account: privateKeyToAccount(privHex) }) : null;
  return { pub, wallet };
}

// Gas for an act. Since Prague (EIP-7623) a transaction with calldata pays at least
// 21000 + 10 per token (zero byte = 1 token, any other byte = 4); for a plain transfer to an
// account that floor is the whole cost. Same rule as the console's figures (hello 21 210,
// fully inked drawing 41 720). A small margin; unused gas is not charged.
export function actGas(calldata) {
  let tokens = 0n;
  for (const b of calldata) tokens += b ? 4n : 1n;
  return 21000n + 10n * tokens + 2000n;
}

// What an act will cost before it is signed: the exact limit and fees, nothing hidden.
export async function quoteAct(net, from, calldata) {
  const { pub } = clients(net);
  const [nonce, block] = await Promise.all([
    pub.getTransactionCount({ address: from, blockTag: 'pending' }),
    pub.getBlock({ blockTag: 'latest' }),
  ]);
  const gas = actGas(calldata), base = block.baseFeePerGas ?? 0n;
  const tip = net.tip ?? 100_000_000n; // 0.1 gwei unless the network says otherwise
  const maxFee = base * 2n + tip;
  return { nonce, gas, tip, maxFee, base, expectedWei: gas * (base + tip), maxWei: gas * maxFee };
}

// One act = one zero-value type-2 transaction, signed here with the monkey's key.
export async function sendAct(net, privHex, to, calldata, quote) {
  const { pub, wallet } = clients(net, privHex);
  const account = wallet.account;
  const q = quote ?? await quoteAct(net, account.address, calldata);
  const tx = {
    type: 'eip1559', chainId: net.chainId, nonce: q.nonce, to, value: 0n, gas: q.gas,
    maxPriorityFeePerGas: q.tip, maxFeePerGas: q.maxFee, data: '0x' + bytesToHex(calldata),
  };
  const raw = await account.signTransaction(tx);
  const hash = await pub.sendRawTransaction({ serializedTransaction: raw });
  return { hash, raw, nonce: q.nonce, gas: q.gas };
}

export async function waitIncluded(net, hash, timeoutMs = 120000) {
  const { pub } = clients(net);
  const r = await pub.waitForTransactionReceipt({ hash, timeout: timeoutMs });
  return { block: r.blockNumber, status: r.status };
}

// Fetch a transaction and check it independently of whoever pointed us to it:
// the sender is recovered from the signature, not taken from the RPC's "from".
export async function verifiedAct(net, hash, fetchFn) {
  const { pub } = clients(net, null, fetchFn);
  const tx = await pub.getTransaction({ hash });
  if (!tx || tx.blockNumber == null) return { ok: false, why: 'not included' };
  if (!decode(hexToBytes(tx.input))) return { ok: false, why: 'not an Okupa act' };
  const receipt = await pub.getTransactionReceipt({ hash });
  if (receipt.status !== 'success') return { ok: false, why: 'reverted' };
  if (tx.type !== 'eip1559') return { ok: false, why: 'not type 2' };
  const signed = serializeTransaction(
    { type: 'eip1559', chainId: tx.chainId, nonce: tx.nonce, to: tx.to, value: tx.value, gas: tx.gas,
      maxFeePerGas: tx.maxFeePerGas, maxPriorityFeePerGas: tx.maxPriorityFeePerGas, data: tx.input, accessList: tx.accessList },
    { r: tx.r, s: tx.s, yParity: tx.yParity });
  if (keccak256(signed) !== hash) return { ok: false, why: 'fields do not hash to the transaction' };
  const from = await recoverTransactionAddress({ serializedTransaction: signed });
  if (from.toLowerCase() !== tx.from.toLowerCase()) return { ok: false, why: 'sender mismatch' };
  if (tx.value !== 0n) return { ok: false, why: 'not zero value' };
  const data = hexToBytes(tx.input);
  const act = decode(data);
  if (!act) return { ok: false, why: 'not an Okupa act' };
  return { ok: true, from: from.toLowerCase(), to: tx.to?.toLowerCase(), block: tx.blockNumber, act, data, hash };
}

export const sameBytes = (a, b) => equal(a, b);
