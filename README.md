# Okupa

**A tiny squatter living in your Nintendo DSi.**

A small monkey moved into a 2009 Nintendo DSi without asking. It lives in the
attic, has needs, sleeps, dreams and talks to you. Its mind runs on 0G Compute,
and the console checks each answer's TEE signature before it believes it. The
console is also a small Ethereum peer of its own: it signs your drawings and
hands them to mainnet nodes over devp2p. There is no RPC on the sending path.

The state of the ETHGlobal Mumbai entry is kept on the `mumbai26` branch.

Everything is C99 on a 133 MHz ARM9 with 16 MB of RAM and a weak Wi-Fi chip.
Press **SELECT** on the console to see the real data behind every step:
discovery, peers, the transaction, blocks, 0G calls and the console itself.

## What runs on the console

| Piece | How |
|---|---|
| Finding Ethereum nodes | EIP-1459 DNS tree (root signature, hashes and every ENR checked), then discv4 |
| Talking to them | RLPx (ECIES, EIP-8), Hello, Status eth/69 with the mainnet fork id, Ping/Pong, snappy |
| Signing | EIP-1559 transactions with libsecp256k1; the key is made on the console from stylus timing |
| Sending | `Transactions` to every node that reaches Status. No RPC, relay or server of ours |
| Inclusion check | eth/69 `GetBlockHeaders`/`GetBlockBodies`: re-hash each header, rebuild the transactions trie, look for our hash. A drawing only hangs on the wall once a block holds it |
| The mind | 0G Compute over TLS 1.2 (BearSSL): a token signed by the console's key, the decision, then the TEE signature checked in C over our own request and response bytes. Only then is the closed-set answer used |
| Senses | Mic → 0G Whisper, camera and drawings → 0G vision (Pictionary, comments), dreams → 0G image model. Each one TEE-verified |

## CROPS

- **C (censorship resistance):** the console finds nodes itself (signed DNS
  list, discv4) and gossips its transaction over devp2p. There is no RPC,
  relayer or server of ours to switch off. If 0G is down, sending still works.
- **O (open source):** MIT. Builds from this repo (`make` on a computer,
  BlocksDS in Docker for the ROM). Dependencies are pinned in `THIRD_PARTY.md`.
- **P (privacy):** like a home node, not a hosted wallet: no account, API key or
  provider that sees all your reads. The first peer sees the IP with the
  transaction, which is public anyway. The 0G provider sees the pet's state it
  is asked about. The wallet balance on screen comes from a public JSON-RPC node
  (publicnode by default, `balance_host=off` in `mind.txt` turns it off). It is
  only for display, and that node learns which IP owns the address.
- **S (security):** the key is born on the console and never leaves it, but it
  is stored in plain text on the SD card. Inclusion is checked against the
  transactions root of a header the peer served. That proves the header lists
  the transaction; it is not consensus or finality, and it trusts one peer.

## 0G: what breaks without it

- **No decisions.** The pet keeps its needs and instincts, but it stops choosing
  what to do and say. The screen says "No answer from 0G: reason". It never
  makes one up.
- **No hearing, sight, Pictionary or dreams.** Each of these is a 0G call with a
  TEE signature that the console checks. Without a verified answer the screen
  says why, and nothing is shown as 0G's.
- **Sending on Ethereum is unaffected.** It never depends on 0G.

## Status (2026-11-07)

Shown on the real DSi:
- the core checks pass on the console: sign a transaction in 30 ms, ECDH in 43 ms;
- DNS discovery and RLPx handshakes with mainnet nodes (Hello from 7 of 25);
- 0G decisions verified on the console itself.

From a computer with the same C core:
- a full mainnet session;
- inclusion checked against live mainnet blocks;
- 0G hearing, sight and dreams verified on mainnet.

Not yet done on the console:
- No mainnet node has kept the console long enough to take a transaction:
  they reply "too many peers". So no transaction from the DSi has been seen
  in a block yet.
- The newest camera, Pictionary, hearing and dream screens are built but not yet
  tried on hardware.

See [docs/STATUS.md](docs/STATUS.md) and [docs/AUDIT.md](docs/AUDIT.md).

## Build

```sh
make && make test && make sanitize           # core and game on a computer
R3D_GL=0 sh tools/build_dsi.sh               # build/dsi/okupa.nds (software 3D)
```

On the SD card, `okupa/mind.txt` names the 0G providers and `okupa/mind.key`
the key that pays them. `okupa/wifi.txt` holds Wi-Fi networks (plain text).

## Layout

```text
core/       portable C: Keccak, RLP, keys, tx, ECIES, RLPx, eth/69, discv4, DNS, TLS, blocks, 0G
game/       the pet, its house, 3D, UI, the mind's prompts, SELECT screens
platform/   dsi (BlocksDS): main loop, network thread ("postman"), mind thread, senses
tools/      computer-side checks: peer, crawl, DNS walk, 0G mind, previews, video
tests/      unit tests with official vectors and real mainnet blocks
```

## How AI was used

- **Code:** written with an AI coding assistant under the author's direction.
  The author chose the design, ran every hardware test on the DSi and reported
  what broke. The assistant drafted and reviewed code, and the tests check it
  against official vectors and real mainnet data. This covers `core/`, `game/`,
  `platform/` and `tools/`. Planning notes are in `docs/`.
- **Art:** attic, outside, ivy, plant and sky textures were made with ChatGPT
  image generation. The prompts and hashes are listed in `THIRD_PARTY.md`.
- **In the game:** every line, guess, description and dream comes from a 0G
  Compute answer whose TEE signature the console has checked.

Secrets, keys and run data live in `local/` and `run/`, never in Git.
