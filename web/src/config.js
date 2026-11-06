// Networks the web monkey can use. Picked in the page; nothing here is secret.
// Each one names where acts live (Ethereum), where pictures live (a Bee node's API)
// and where the mailbox lives (an Arkiv chain).
export const NETWORKS = {
  local: {
    name: 'Local dev stack',
    label: 'Local dev stack: Geth --dev, a Bee dev node and a local Arkiv node. Not public networks.',
    chainId: 1337, rpc: 'http://127.0.0.1:8545', scanBlocks: 600, devFaucet: true,
    bee: 'http://127.0.0.1:1633',
    arkiv: { name: 'Arkiv local', chainId: 1337, rpc: 'http://127.0.0.1:8547', symbol: 'GLM', days: 1, devFaucet: true },
  },
  sepolia: {
    name: 'Sepolia',
    label: 'Sepolia testnet through a public RPC; Swarm through your own Bee node; Arkiv testnet Rabdi.',
    chainId: 11155111, rpc: 'https://ethereum-sepolia-rpc.publicnode.com',
    explorer: 'https://sepolia.etherscan.io', blockscout: 'https://eth-sepolia.blockscout.com',
    bee: 'http://127.0.0.1:1633',
    arkiv: { name: 'Rabdi', chainId: 7794095, rpc: 'https://rpc.rabdi.db-chain.testnet.arkiv.network', symbol: 'tGLM', days: 7 },
  },
  mainnet: {
    name: 'Ethereum',
    label: 'Ethereum mainnet: each act shows its cost and asks first. Pictures on Swarm, mailbox on Arkiv Rabdi.',
    chainId: 1, rpc: 'https://ethereum-rpc.publicnode.com', tip: 20_000_000n, confirmSpend: true,
    explorer: 'https://etherscan.io', blockscout: 'https://eth.blockscout.com',
    bee: 'http://127.0.0.1:1733', // Bee light node on Swarm mainnet (Gnosis)
    arkiv: { name: 'Rabdi', chainId: 7794095, rpc: 'https://rpc.rabdi.db-chain.testnet.arkiv.network', symbol: 'tGLM', days: 7 },
  },
};
