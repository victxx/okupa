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
| Signing | EIP-1559 transactions with libsecp256k1; the key is made on the console from stylus timing, mixed with a one-use seed from a computer when `seeds.bin` is there |
| Sending | `Transactions` to every node that keeps us, the most reliable first. No RPC, relay or server of ours. Saved before it is sent, so a retry (even after a restart) sends the same bytes; a stuck one can be sped up (same nonce, twice the fee) |
| Inclusion check | eth/69 `GetBlockHeaders`/`GetBlockBodies`: re-hash each header, rebuild the transactions trie, look for our hash (any version of it). Two different nodes must serve the same block with it before a picture hangs on the wall |
| Pictures | 64x64 drawings (1 bit) and camera photos in 4 tones (1 KB), whole in the calldata. `tools/gallery` reads them back from the chain with any read-only RPC |
| Saving | One snapshot (pet, page, wall, nonce, the transaction as signed) in two files that take turns, synced and read back; damage never overwrites the last good one |
| Games | Banana catch and copy me on the console; Pictionary with a friend on any phone or computer on the same Wi-Fi (the DS serves the page); a friend's drawing can be hung on the wall |
| The mind | Its own small mind on the console (needs, hour, what you did, a phrase book), or 0G Compute when it is set up: a token signed by the console's key, then the TEE signature checked in C over our own request and response bytes; only then is the closed-set answer used |
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
  transactions root of a header that two different peers served. That proves
  those headers list the transaction; it is not consensus or finality (see
  docs/RESEARCH.md for a light client that would be).

## 0G is optional

- **Without it** the okupa has its own small mind: it moves, speaks and
  daydreams from its own phrase book, and every game, the camera, the wall and
  Ethereum work. Nothing it says that way is shown as 0G's.
- **With it** (`mind.txt` and `mind.key` on the SD) its decisions, hearing,
  sight, Pictionary guesses and dreams come from 0G, each answer checked
  against its TEE signature on the console before it is used.

## Status (2026-11-07, the Mumbai entry; kept on the `mumbai26` branch)

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
make film                                    # bake the opening film (build/film.okf, goes in the ROM)
R3D_GL=0 sh tools/build_dsi.sh               # build/dsi/okupa.nds (software 3D)
```

On the SD card (`okupa/`): `wifi.txt` holds Wi-Fi networks (plain text);
`seeds.bin` one-use entropy (`build/okupa-seeds sd/okupa/seeds.bin`, copy it
once, never keep a copy); `mind.txt` and `mind.key` turn on the 0G mind;
`state-a.bin`/`state-b.bin` are the save. To try the friend page on a
computer: `make build/okupa-friend && build/okupa-friend`.

## Layout

```text
core/       portable C: Keccak, RLP, keys, tx, ECIES, RLPx, eth/69, discv4, DNS, TLS, blocks, 0G
game/       the pet, its house, 3D, UI, the mind's prompts, SELECT screens
platform/   dsi (BlocksDS): main loop, network thread ("postman"), mind thread, senses
tools/      computer-side: peer, crawl, DNS walk, 0G mind, seeds, the gallery page, the friend page and server, previews
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
- **In the game:** a line from 0G is used only once its TEE signature is
  checked on the console; the rest come from the okupa's own phrase book.

Secrets, keys and run data live in `local/` and `run/`, never in Git.
