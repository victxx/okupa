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
| **`06`** | **big photo (proposed, never calldata)** | The file on Swarm: `OKPA 01 06` + 256×192 2 bpp (12,288 B) | web cave; DSi cave later |

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

## Two kinds of gift: small on the wall, big in the cave

| | Small gift | Big gift |
|---|---|---|
| Picture | 64×64 drawing | 256×192 photo in four tones (the DSi's screen size), dithered from any image on the sender's computer |
| Where the picture lives | Inside the Ethereum transaction (`OKPA 01 03`) | On Swarm, encrypted (`OKPA 01 06`, 12 KB) |
| What the transaction carries | The picture | The sealed Swarm reference (`OKPA 01 05`, 215 B): the same size as a small Swarm gift |
| Cost on mainnet today | ≈ €0.014 | ≈ €0.010 + a share of a Swarm postage batch (≈ €0.06 for 30 days, holding thousands of pictures) |
| Found through | The chain (the DSi following blocks), or Arkiv | Arkiv (web), or the chain (DSi) |
| Hangs | On the wall | In **the cave**, a darker room for big pictures, opened full size |

The rule is easy to say:
- the chain carries the act;
- Swarm carries the bulk;
- Arkiv is the mailbox.

A big gift is something the DSi cannot make itself, since it has no web and no photo
library. A friend on another platform can give it one.

Tested locally: monkey A turned an image into a big picture, sent it, and monkey B found it
in Arkiv, checked it, opened it and put it in the cave. Evidence:
`run/web-cave-20261107/evidence.json`, tx `0x72870234…261c`, entity `0x50edc786…dd7a`.

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
- **O (open source):** everything is open-licensed (MIT/BSD dependencies, listed below). The repository is public: https://github.com/victxx/okupa.
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
| Same flow in public: Ethereum mainnet + Swarm mainnet + Arkiv Rabdi | **Real.** A gave B three gifts: inline (kind 03), small via Swarm and big via Swarm (kind 05). B checked each and hung two on the attic wall; the third went to the cave. Each send showed its cost and was approved first. A command-line read of B's mailbox passes all three chain checks | `run/web-public-20261107/`. Inline: tx `0x5c20ad2c…d98a`, block 26358502. Small via Swarm: tx `0x2bed6768…7f4c`, block 26358504, Arkiv `0xcf06a477…3984`. Big via Swarm: tx `0xaa04cef6…5477`, block 26358505, Arkiv `0x220b9dd6…4301` |
| Big gift → the cave (web ↔ web) | **Real** (local) | `run/web-cave-20261107/` |
| Mainnet sends with cost shown and approval asked first; gas from the EIP-7623 floor | **Real**: three approved sends at 0.066–0.069 gwei base fee, about 0.000003–0.000004 ETH each | `run/web-public-20261107/evidence.json` |
| Bee light node on Swarm mainnet | **Real**: Bee 2.8.3 light node. Postage batch `b7c6193f…5134`, depth 17, about 29 days, tx `0x51ba5b20…58b7` on Gnosis. Bee's default fee (125 wei tip) was dropped by the public RPC; `Gas-Price: 2000000000` worked | node `0x5AC1Fd51…16FE` |
| DSi reads gifts sent to it | **Design only**, see below | |
| Kind 05 in `core/` | **Next.** Proposed here; `core/` not touched | |

Public sends: only the three approved mainnet gifts above, plus the Bee node's Gnosis transactions (chequebook, approve, postage batch).

## How the DSi takes part (design; not built tonight)

A monkey is an Ethereum account that speaks `OKPA 01 xx`. A DSi monkey and a web monkey
are friends in the same way two DSi monkeys would be.

| Step | DSi | Web | Built |
|---|---|---|---|
| Become friends | Shows its friend code as a QR (address + public key) | Scans or pastes it | DSi shows an address QR today; the public key needs to be added |
| Web visits DSi | — | Reads its hellos and drawings from the chain, checks each signature | **Yes** |
| Small gift web → DSi | Follows new blocks over P2P (as for its own inclusion check), finds an `OKPA 01 03` transaction to itself from a friend, hangs it | Sends it | Web side **yes**; DSi side next (`core/` + `game/`) |
| Small gift DSi → web | Signs `OKPA 01 03` to the friend's address and gossips it over devp2p | Finds it in a visit, or in Arkiv if someone indexed it | DSi side next |
| Big gift web → DSi | Sees the `OKPA 01 05` transaction in a block, opens the seal with its key (the ECIES it already has), fetches the encrypted chunks from a public Swarm gateway over HTTPS, decrypts them on the console, checks the keccak, shows the photo in its cave | Sends it | Web side **yes**; DSi side later |
| Gifts that arrived while it was off | Asks Arkiv `to = me` over HTTPS for (tx, block) hints, then checks each block over P2P | — | Later |

Notes:
- **TLS.** `api.gateway.ethswarm.org` and Rabdi's RPC both negotiate TLS 1.2, checked with `openssl s_client`. That is what the console's BearSSL already uses for 0G; each needs a pinned root. Not tried on hardware.
- **The DSi fetches chunks, not the picture.** It never sends the decryption key: it asks for the encrypted chunks by address and decrypts them itself. Swarm encrypts chunks with a Keccak-based keystream, and the core already has Keccak. So a gateway can withhold a picture, but it cannot read or change it.
- **Arkiv is only a hint.** Every gift is checked against block bodies from several independent peers: header hash, `transactionsRoot`, then `receiptsRoot` for success. Like the console's own inclusion check, this trusts that the peers do not all lie together. It does not verify consensus.
- **Friends only.** The DSi shows a gift only if the sender is in its friends list.
- **The sending path stays pure.** The DSi still signs and gossips over devp2p only. Swarm and Arkiv are used for reading bulk bytes and hints, never for sending.

## Booth demo for the Swarm and Arkiv teams (about 2 minutes)

Two laptops (or two browser windows), monkey A and monkey B, and the DSi on the table.

1. **Friends (10 s).** B shows its friend code; A pastes it.
2. **Small gift (20 s).** A draws a 64×64 picture and sends it. The transaction carries the picture itself. B presses **Check** and it hangs on the wall.
3. **Big gift (40 s).** A drops in a photo from the laptop. It becomes 256×192 in four tones and is uploaded to Swarm, encrypted. Show the log:
   - the Swarm chunk address (the key half stays secret);
   - the 215 B sealed transaction;
   - the Arkiv entity.

   B checks the mailbox and the photo appears in **the cave**. Click it to see it full size.
4. **"Can Arkiv lie?" (20 s).** Run `node web/scripts/lab.mjs forge <B> <tx>` and check again. Two fake entries are rejected with the reason shown: the index can hide, never forge.
5. **"Is it really private?" (15 s).** Open the Swarm chunk address in a gateway: noise. Open the Arkiv entity in the explorer: a sealed blob. Only B's key opens it.
6. **The DSi (15 s).** Visit the DSi's address from the web and show its hellos and drawings, signed on the console and sent with no RPC. Close with: "Next, the DSi opens the cave."

**What to have ready:**
- the Bee light node on Swarm mainnet with a postage batch;
- both monkeys funded with mainnet ETH and Rabdi tGLM;
- a fallback: the local stack (`web/scripts/stack.sh up`), in case the venue Wi-Fi blocks something.

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
| Public repo, open-source license, AI-use statement | Public repo: yes. Still no LICENSE file. AI use is stated below |
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
