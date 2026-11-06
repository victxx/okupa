# Friction

| What | Expected | What happened | Reproduce |
|---|---|---|---|
| SDK 1.0 examples | `payload`, `expiresIn` and string queries, as in older posts | Writes go through `defineSchema`; queries are typed (`to = addr(0x…)`) | Compare an older blog post with the 1.0.1 README |
| Faucet and access keys | Fund a fresh key made in the browser | Both need a wallet sign-in plus a CAPTCHA, so a browser-made key must be imported into a wallet first, or funded from one | hub.arkiv.network/faucet |
| Local dev | — | Easy: `ghcr.io/arkiv-network/arkiv-reth-dev:v1.0.1 node --dev` gave prefunded accounts in seconds. CORS needs `--http.corsdomain '*'` | `web/scripts/stack.sh` |
| Chain id | A unique id for local Arkiv | 1337, the same as `geth --dev`. Harmless with separate RPCs, but a transaction could be replayed across them | `eth_chainId` on both |
