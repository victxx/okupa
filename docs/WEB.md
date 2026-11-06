# Okupa web: gifts between monkeys with Swarm and Arkiv

A separate web version of Okupa, built to show the Swarm and Arkiv teams how pictures
and gifts between monkeys could work. It is **not** part of the EF/0G entry, and it
does not change the console. The DSi keeps its strong property: it sends by devp2p
with no RPC, proxy or relay. The browser cannot do that, so the web monkey reads and
sends through JSON-RPC and says so on screen.

Code: `web/`. How to run it: [web/README.md](../web/README.md).

## Roles

| Layer | Holds | Trusted for |
|---|---|---|
| Ethereum | The signed act: one zero-value type-2 transaction per gift, calldata in the `OKPA 01 xx` family | Who sent what to whom, and when. This is the only source of truth |
| Swarm | The picture, uploaded with `Swarm-Encrypt: true`. Without the 64-byte reference (address and key) the chunks are noise | Nothing. A Bee node can withhold a picture, but cannot swap it: the sealed keccak catches any change |
| Arkiv | A mailbox index: one entity per gift (to, from, kind, chain, tx, block, sealed reference) | Nothing. It lets a recipient find gifts without scanning the chain. It can hide a gift, but it cannot forge one |

```text
 monkey A (browser)                                                  monkey B (browser)
  draw 64x64, four tones                                              Arkiv: entities with to = B
   │ picture (OKPA 01 02, 1030 B)                                        │ for each entry:
   ├──► Bee  POST /bytes  Swarm-Encrypt ─► ref = addr32‖key32            │  owner == from (Arkiv signer)
   │ seal = ECIES(B.pub, ref ‖ keccak(picture), auth = header‖B.addr)    │  tx: sender recovered from the
   ├──► Ethereum  tx A→B  calldata OKPA 01 05 ‖ seal (215 B)  ──────────►│  signature == from, to == B,
   └──► Arkiv  createEntity(okupa-gift-v1, {to:B, from:A, tx, seal})    │  calldata seal == entry seal
                                                                         │ open seal with B's key → ref, hash
                                                                         │ Bee GET /bytes/ref → keccak == hash
                                                                         └► hang on the wall
```

## Formats

The DSi format is unchanged. The web version uses three existing kinds and proposes one
new kind.

| Kind | Name | Calldata | Who reads it |
|---|---|---|---|
| `00` | hello | `OKPA 01 00` | DSi, web (visit) |
| `01` | drawing | `OKPA 01 01` + 64×64 1 bpp (512 B) | DSi, web (visit) |
| `02` | photo | `OKPA 01 02` + 64×64 2 bpp (1024 B) | web. On Swarm it is the picture file itself |
| `03` | gift picture, inline | `OKPA 01 03` + drawing or photo | web now; the DSi later. This is the web → DSi form |
| **`05`** | **gift picture on Swarm (proposed)** | `OKPA 01 05` + seal (209 B) | web |

**The seal (kind 05)**
- Plaintext (96 B): Swarm encrypted reference (chunk address 32 ‖ decryption key 32) ‖ `keccak256(picture)` (32).
- Cipher: ECIES exactly as the console does it for RLPx (`core/src/ecies.c`): `0x04 ‖ R(64) ‖ iv(16) ‖ AES-128-CTR ‖ HMAC-SHA256(32)`, concat KDF over SHA-256.
- The MAC also covers `OKPA 01 05 ‖ recipient address` as authenticated data. A seal cannot be replayed as another kind, or towards someone else.
- 209 B sealed, 215 B of calldata. That is cheaper than an inline photo (1030 B), and the picture behind it can grow without the transaction growing.
- `web/test/crypto.test.js` checks the browser ECIES against the same vectors as `tests/test_ecies.c`: the EIP-8 handshakes and the independent Python encoder. The bytes are identical, so a DSi could open a seal with code it already has.

**Arkiv schema `okupa-gift-v1`**

| Field | Type | Indexed | Why |
|---|---|---|---|
| `to` | addr | yes | The mailbox query: `to = B AND chain = id` |
| `from` | addr | yes | Claimed sender. Never believed on its own: it must equal both `$owner` and the transaction's recovered sender |
| `kind` | u64 | yes | Act kind (3 or 5) |
| `chain` | u64 | yes | Ethereum chain id of the transaction, so one index serves several networks |
| `tx` | str | no | The transaction hash to verify |
| `block` | u64 | no | A hint only; the chain decides |
| `sealed` | bytes | no | The same seal the transaction carries. Empty for inline gifts |

- **Owner:** the sender's own key, the same secp256k1 key as on Ethereum. Arkiv's chain sets `$owner`, which ties the entry to a signer.
- **Expiry:** 7 days on the testnet, 1 day locally. A mailbox is a notification, not the record; the gift stays on Ethereum forever.
- **Reference:** Arkiv entities are public. The prize page warns about this too: an encrypted Swarm reference contains its key. So Arkiv holds only the seal, never the raw reference.

## What the receiver checks before showing anything

1. The Arkiv entry's `$owner` equals its `from`.
2. The transaction is included and succeeded, it is type 2 with value 0, and its fields re-hash to its hash.
3. The sender is recovered from the signature, and must equal `from`. The RPC's "from" field is not trusted.
4. `to` is this monkey, the calldata decodes to exactly one Okupa act, and the kinds match.
5. For kind 05: the calldata seal equals the entry's seal byte for byte. The seal must open with this monkey's key under the bound header and address.
6. The bytes the Bee node returns must hash to the sealed keccak, and must decode as a 64×64 four-tone picture.

**Tested:**
- `web/scripts/lab.mjs forge` writes two fake entries into a real mailbox, both pointing at a real gift transaction:
  - one claims the real sender while being signed by another key: rejected with "entry not written by the sender it names";
  - one is signed by the forger under its own name: rejected with "transaction sender differs from the entry".
- The page and `web/scripts/read-mailbox.mjs` agree on both rejections (`run/web-local-20261106/mailbox-B.json`).

## Trust model, CROPS

**Web monkey**
- **C (censorship resistance):** weaker than the DSi. The web monkey sends through an RPC endpoint that can refuse it, and switching endpoints is the only remedy. Arkiv can hide a mailbox entry and a Bee node can withhold a picture. Neither can forge a gift, and the gift itself stays on Ethereum, so anyone can rebuild a wall from the chain. Arkiv entries expire, and pictures stay on Swarm only while their postage batch is paid for.
- **O (open source):** everything is open-licensed (MIT/BSD dependencies, listed below). The repository is still private.
- **P (privacy):**
  - Kept private: picture contents. They are encrypted on Swarm, and only the recipient can open the seal.
  - Public: who gave to whom and when (Ethereum, Arkiv).
  - Visits can read through anon-rpc, so the explorer and RPC do not learn who is looking.
  - Sends are not anonymised: the RPC sees the sender's IP.
- **S (security):**
  - Keys: the key is made in the browser (WebCrypto RNG through noble) and never leaves it, but it sits in `localStorage` of that origin, which is far weaker than the console. Any script on that origin can read it, and there is no backup: clear site data and the monkey is gone.
  - Primitives: reviewed noble libraries, versions pinned.
  - The seal is MAC-checked before decryption.

**DSi monkey (unchanged by this work)**
- **C:** the DSi signs and gossips over devp2p with no RPC.
- **S:** the DSi's key stays on the SD card and the console.

## Real tonight vs next

| Piece | State | Evidence |
|---|---|---|
| Two browser monkeys (two origins, two keys) | **Real**, on a local dev stack: Geth 1.17.7 `--dev`, Bee 2.8.0 dev mode, `arkiv-reth-dev` v1.0.1 | `run/web-local-20261106/evidence.json` |
| Monkey A draws → Swarm (encrypted) → seal → Ethereum tx → Arkiv entry | **Real** (local) | tx `0x75461171…64b5`, block 274; Arkiv entity `0xd7787e35…6329`; Swarm encrypted root chunk `5c310842…0031`, batch `458ec8cb…309e` |
| Monkey B finds via Arkiv, verifies on chain, opens, fetches, checks hash, hangs | **Real** (local) | `mailbox-B.json`, screenshot `monkey-B-wall-and-visit.jpg` |
| Forged mailbox entries rejected | **Real** (local) | `forge.json`, `mailbox-B.json` |
| Visit: a DSi-format hello and drawing signed by the console's C core, read and checked in the web app | **Real** (local). `web/tools/dsi-act.c` signs with `core/` (same `ok_act_*`, `ok_tx_sign` as the ROM), but on the Mac, not on a DSi | `dsi-standin.json` |
| Web → DSi inline gift (kind 03, 1 bpp) | **Real** (local, to the DSi stand-in's address) | tx `0xb5a28169…cd8a` |
| Visit on Sepolia/mainnet | Works through Blockscout (finds) + RPC (verifies). No real DSi act on a public chain was available to show at the time of writing | |
| anon-rpc reads for visits | **Real**: passthrough (mainnet specifier) and Nox mixnet (Sepolia specifier) work. Tor failed tonight (demo gateway timeout) | `run/web-anonrpc-20261106/evidence.json` |
| Same flow on public testnets (Sepolia + Swarm testnet + Arkiv Rabdi) | **Next.** Not done. Needs: Sepolia ETH for each web monkey; tGLM from the Rabdi faucet (wallet sign-in + CAPTCHA, which only a person can do); a Bee light node on the Swarm testnet with sBZZ for a postage batch | |
| DSi reads gifts sent to it | **Design only**, see below | |
| Kind 05 in `core/` | **Next.** Proposed here; `core/` not touched | |

Nothing was sent to any public network, and no real funds were spent.

## Design: the DSi receiving a gift (not built)

The DSi cannot use kind 05 without reaching a Swarm gateway, so it accepts **inline gifts
(kind 03)** only. They are self-contained in the transaction, like its own drawings.

1. **Where to look.** A gift to the DSi is a transaction *to* its address. The DSi does not
   scan the chain. It asks the Arkiv mailbox for `to = me AND chain = id` and gets
   (tx hash, block number) pairs. Rabdi's RPC is HTTPS JSON-RPC, and it negotiates TLS 1.2
   with ECDHE-ECDSA (checked with `openssl s_client`), which the console's BearSSL already
   speaks to 0G. So this read could be direct, needing only a pinned root, with no gateway
   of ours. Not tried on hardware. Arkiv is a hint here, never a source of truth.
2. **Check it over P2P.**
   - For each hint, the DSi asks several eth/69 peers for that block's header and body. This is the same path as `ok_block` and its inclusion check on `main`.
   - It recomputes the header hash and `transactionsRoot`, finds the transaction, and recovers the sender.
   - It checks `to == me`, value 0 and an `OKPA 01 03` payload.
   - Receipts (`GetReceipts`, `receiptsRoot`) prove success.
   - Like the console's own inclusion check, this trusts that several independent peers do not lie at once. It does not verify consensus.
3. **Friends only.** It shows the gift only if the sender is in its friends list (a QR friend code), as `docs/PLAN.md` says.
4. **Without Arkiv.** The DSi can still find gifts by following new blocks while it holds a session. Arkiv only saves it from scanning blocks it missed while switched off.

## Prize fit (ETHGlobal Mumbai, Arkiv)

Read from https://ethglobal.com/events/mumbai/prizes on 2026-11-06. This is a practice
build; nothing is being submitted.

**Build on Arkiv (from scratch), $4,000**

| Requirement | Status |
|---|---|
| Built from scratch at the event | Partly. `web/` was started tonight, but it reuses Okupa's formats and ECIES (the same repo). Judges may see that as prior work |
| Writes to Arkiv's **current testnet** (Rabdi) with the SDK or JSON-RPC | **Missed so far.** Writes go to a local `arkiv-reth-dev` node; the code already targets Rabdi (`web/src/config.js`). Blocked on tGLM from the faucet |
| At least one user-visible feature reads from Arkiv | **Met.** The mailbox drives the wall |
| README lists writer wallet(s) and one runnable query | Missing (local addresses only). The query exists: `scripts/read-mailbox.mjs <net> <address>` |
| `/arkiv/schema.md`, `friction.md`, `reach.md`, `why-arkiv.md`, `how-you-use-arkiv.md` | Missing. The content is in this file (schema, owners, expiry, trust) |
| Public repo, open-source license, AI-use statement | Missed: the repo is private and has no LICENSE. AI use is stated below |
| Not "SDK installed but never read" / "a log nothing reads" | Met: B's wall exists only through Arkiv reads |

**Arkiv + Swarm bonus, $500 (needs an Arkiv track entry; "a project that uses only Swarm does not qualify")**

| Requirement | Status |
|---|---|
| Uses both Arkiv and Swarm meaningfully | **Met** in design and locally. Swarm holds the user-visible picture, encrypted. Arkiv points to it through the `sealed` field. This follows the page's own warning: the reference is never public |
| One entity key + the Swarm reference it points to, with a link that opens | Local only (entity `0xd7787e35…`, chunk `5c310842…`). There is no public link until Rabdi plus a public Bee node |
| Postage batch ID, who bought it, which Bee node | Local: batch `458ec8cb…309e`, bought by Bee 2.8.0 in dev mode (no chain), uploaded through `http://127.0.0.1:1633` |
| Files that upload to and read from Swarm | `web/src/swarm.js` (upload/download), `web/src/app.js` (`sendSwarmGift`, `checkMailbox`) |
| Steps to run one upload and one read | `web/README.md` |
| Video timestamp | None (no video) |
| What got in the way | Bee dropped dev mode after 2.8.0 (pinned 2.8.0). A public upload needs a funded node and stamp |

**Other observations**
- There is no standalone Swarm prize at Mumbai; Swarm appears only inside the Arkiv bonus.
- Arkiv judges ask "what would users lose with Postgres". Here: anyone can rebuild a mailbox from a public index whose entries are signed by their senders, and no Okupa server exists to host or censor it.

## Friction notes (for the Arkiv and Swarm teams)

- **Arkiv SDK 1.0.** The SDK has changed from older examples: there is no `payload` or `expiresIn`, writes go through a schema, and queries are typed (`to = addr(0x…)`). This was fine once read, but most blog posts are outdated.
- **Arkiv dev image.** `ghcr.io/arkiv-network/arkiv-reth-dev` made a local mailbox possible in minutes: prefunded unlocked accounts, CORS with `--http.corsdomain`.
  - It shares chain id 1337 with `geth --dev`. That is harmless here (separate RPCs), but a transaction for one could be replayed on the other.
- **Arkiv faucet and access keys.** Both require a wallet sign-in plus a CAPTCHA. A browser-made key cannot use them without being imported into a wallet first.
- **Bee dev mode.** `bee dev` is gone after 2.8.0, so 2.8.0 is pinned here. Encrypted upload works the same in dev mode: 128-hex reference, CORS via `--cors-allowed-origins`.
- **anon-rpc.** The browser harness is about ten lines to wire into viem through `fetchFn`. The Tor demo gateway was down tonight; Nox on Sepolia worked.

## Dependencies and licenses (web/)

| Package | Version | License |
|---|---|---|
| @noble/curves, @noble/hashes, @noble/ciphers | 2.4.0 | MIT |
| viem | 2.57.16 | MIT |
| @arkiv-network/sdk | 1.0.1 | MIT (LICENSE.md) |
| @anon-rpc/browser-harness, @anon-rpc/harness-core | 0.4.0 | MIT |
| vite (dev only) | 8.3.6 | MIT |
| ethersphere/bee (docker, lab only) | 2.8.0 | BSD-3-Clause |
| arkiv-reth-dev (docker, lab only) | v1.0.1 | MIT |

## AI use

The `web/` code, its tests and this document were written with an AI coding assistant,
under the user's direction. The behaviour was checked by tests (`npm test`) and by the
local evidence runs listed above.
