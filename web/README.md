# Okupa web

A browser monkey that trades pictures with other monkeys:
- Ethereum carries the signed acts.
- Swarm holds the pictures, encrypted.
- Arkiv is an untrusted mailbox index.

Design, trust model and prize fit are in [docs/WEB.md](../docs/WEB.md).

A browser cannot open TCP, so this monkey reads and sends through JSON-RPC. The DSi
doesn't need that: it talks devp2p directly.

## Run it locally (no funds needed)

You need Node 22+, Docker and a `geth` binary (1.17.7 tested).

```bash
cd web && npm ci
```

```bash
GETH=/path/to/geth sh scripts/stack.sh up
```

```bash
npm run dev
```

The stack script starts three local dev nodes:

| Node | Address | Notes |
|---|---|---|
| Geth `--dev` | :8545 | Ethereum |
| Bee 2.8.0 in dev mode | :1633 | Swarm. It buys a postage batch with no chain |
| `arkiv-reth-dev` v1.0.1 | :8547 | Arkiv |

Open two monkeys. Two origins means two separate keys:
- monkey A: http://127.0.0.1:5173
- monkey B: http://localhost:5173

1. On each monkey, press **Fund from local dev chains**. The dev nodes' unlocked accounts send it 1 coin each, on Geth and on Arkiv.
2. On B, copy the **Friend code** (`0xADDRESS:PUBLICKEY`).
3. On A, draw, paste B's code in **To**, then press **Send privately**. What really happened is listed at the bottom of the page.
4. On B, press **Check**. The gift is found in Arkiv, checked on chain, its seal opened, fetched from Swarm, hash-checked, then hung on the wall.
5. **Send small gift** puts a 64×64 one-bit drawing inside the transaction (`OKPA 01 03`). The DSi reads that form.
6. **Visit a monkey** shows any address's hellos, drawings and gifts:
   - On local, it scans recent blocks.
   - On Sepolia/mainnet, Blockscout finds the transactions and RPC checks each one.
   - Reads can go through anon-rpc. Nox on Sepolia works; the Tor demo gateway may be down.

## Lab helpers

```bash
npm test
```

Runs the ECIES and calldata tests, with the console's vectors.

```bash
sh tools/build-dsi-act.sh
```

Builds `build/web/dsi-act`, which signs acts with `core/`. Set `SECP=` if the submodule lives elsewhere.

```bash
node scripts/lab.mjs dsi ../run/web-stack
```

A DSi stand-in key signs a hello and a drawing with the C core and sends them to Geth `--dev`.

```bash
node scripts/lab.mjs forge <B address> <gift tx>
```

Writes two forged mailbox entries. B's page must reject both.

```bash
node scripts/read-mailbox.mjs local <address>
```

Reads a mailbox from the command line, with the same chain checks.

```bash
sh scripts/stack.sh down
```

Stops the local stack.

## Public run (Ethereum mainnet + Swarm mainnet + Arkiv Rabdi)

Choose **Ethereum** in the page. Each act shows its cost and asks before signing. You need:

1. **A Bee light node on Swarm mainnet** (Gnosis), with its API on `:1733`. The password file and data directory live under `local/`:

   ```bash
   docker run -d --name okupa-bee-mainnet --restart unless-stopped -p 127.0.0.1:1733:1633 -p 1734:1634 -v "$PWD/../../local/swarm-mainnet/data:/home/bee/.bee" -v "$PWD/../../local/swarm-mainnet/password:/run/bee-password:ro" ethersphere/bee:2.8.3 start --api-addr=:1633 --full-node=false --swap-enable=true --swap-initial-deposit=0 --blockchain-rpc-endpoint=https://gnosis-rpc.publicnode.com --password-file=/run/bee-password --cors-allowed-origins='*'
   ```

   Fund its Gnosis address with a little xDAI (gas) and xBZZ, then buy one postage batch. At depth 17, for 30 days, that is ≈ 1.4 xBZZ:

   ```bash
   curl -X POST localhost:1733/stamps/<amount>/17
   ```

2. **A little mainnet ETH for each monkey.** A gift costs about €0.01–0.02 at 0.15 gwei.
3. **tGLM on Rabdi for each monkey**, from the Arkiv faucet.

Check what is funded:

```bash
node scripts/funding.mjs <monkey>... --bee <bee address>
```

## Files

| File | Job |
|---|---|
| `src/okupa.js` | Calldata, same family as the DSi (`OKPA 01 xx`) |
| `src/ecies.js` | Same ECIES as `core/src/ecies.c` |
| `src/seal.js` | The kind-05 seal |
| `src/swarm.js` | Bee upload (encrypted) and download |
| `src/arkiv.js` | Mailbox schema, write and query |
| `src/chain.js` | Send through RPC; independent checks of a transaction |
| `src/anon.js` | anon-rpc reads |
| `src/app.js` | The page: landing, gifts, wall and archive |
| `public/download/` | `okupa.nds`, `README.txt` (SD guide), `rom.json` (commit, sha256). The ROM is not in Git (`*.nds` is ignored): `sh scripts/rom.sh <okupa.nds> <commit>` copies a build there before you serve or publish the page |
| `src/scene.js`, `scene/scene.c` | The attic: the console's renderer compiled to WebAssembly. `sh scene/build.sh` rebuilds `public/scene.wasm` (needs clang's wasm32 target and `wasm-ld`, e.g. `brew install lld`) |

Keys live only in the browser's local storage. Run data goes in `../run/`, never in Git.
