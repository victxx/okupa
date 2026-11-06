// Balances that the public run needs, read-only:
//   node scripts/funding.mjs <monkey address>... [--bee 0xBEE_NODE]
// Ethereum mainnet ETH and Arkiv Rabdi tGLM per monkey; Gnosis xDAI and xBZZ for the Bee node.
import { createPublicClient, http, formatEther, formatUnits, erc20Abi } from 'viem';
import { NETWORKS } from '../src/config.js';
import { arkivChain } from '../src/arkiv.js';
import { chainOf } from '../src/chain.js';

const args = process.argv.slice(2), b = args.indexOf('--bee');
const bee = b >= 0 ? args.splice(b, 2)[1] : null;
const eth = createPublicClient({ chain: chainOf(NETWORKS.mainnet), transport: http(NETWORKS.mainnet.rpc) });
const ark = createPublicClient({ chain: arkivChain(NETWORKS.mainnet.arkiv), transport: http(NETWORKS.mainnet.arkiv.rpc) });
for (const a of args) {
  console.log(a, 'ETH', formatEther(await eth.getBalance({ address: a })), '| tGLM', formatEther(await ark.getBalance({ address: a })));
}
if (bee) {
  const gnosis = createPublicClient({ chain: { id: 100, name: 'Gnosis', nativeCurrency: { name: 'xDAI', symbol: 'xDAI', decimals: 18 }, rpcUrls: { default: { http: ['https://gnosis-rpc.publicnode.com'] } } }, transport: http('https://gnosis-rpc.publicnode.com') });
  const bzz = await gnosis.readContract({ address: '0xdBF3Ea6F5beE45c02255B2c26a16F300502F68da', abi: erc20Abi, functionName: 'balanceOf', args: [bee] });
  console.log(bee, 'xDAI', formatEther(await gnosis.getBalance({ address: bee })), '| xBZZ', formatUnits(bzz, 16));
}
process.exit(0);
