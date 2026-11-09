# Research: a QR signer and a light client on the DSi (2026-11-09)

Two ways the console could be useful to Ethereum beyond the game. Numbers
below were produced on a computer, not on the DSi: the code was compiled with
the pinned BlocksDS toolchain for the ARM946E-S and run in an instruction-set
emulator (Unicorn, ARM946 model), then scaled by the factor between that model
and what the DSi really measured for secp256k1 (3.4-4.1x slower, consistent
across sign, pubkey and ECDH). A timing ROM to replace them with real numbers
was built (run/research-front3/out/arm9/blsbench.nds, not in Git).

## A. Air-gapped signer for real wallets (ERC-4527): feasible, do first

- MetaMask (extension and mobile, "Other QR wallet") and others pair with a
  `crypto-hdkey` QR (compressed key, chain code, path m/44'/60'/0', source
  fingerprint) and then show `eth-sign-request` QRs; the device answers with
  an `eth-signature` QR (r, s, v). Requests up to ~200 bytes are one static
  QR: an ETH transfer is version 8, an ERC-20 transfer version 10, the reply
  version 7, the pairing version 9. EIP-712 and big calls are animated
  (fountain-coded UR, about 5-6 frames).
- The DSi screen shows up to version 9 at 3 px a module, 17 at 2 px: every
  answer the console shows is static.
- Reading QRs with the camera: quirc (ISC) decodes, but stock it is ~10 s a
  frame here (soft float). With float rounding instead of rint and one
  refinement pass: ~1.8-2.3 s a frame; fixed-point perspective maths should
  bring it to ~0.6-0.8 s (estimate). 330 KB RAM at 640x480.
- To write in C: BIP39/BIP32 (+RIPEMD-160), CBOR, UR with fountain decoding,
  ERC-4527 types, unsigned-transaction RLP, EIP-712 hashing, clear signing of
  common calls (transfer, approve, permit), camera capture at 640x480 Y and
  the quirc patch, the screens: about 3,000 lines. Two weeks for static
  transfers with MetaMask, four to five with animated QRs and EIP-712.
- Honest limits: no secure element; the key would sit on the SD encrypted
  under a passphrase; whoever has the console and card can attack it offline;
  calls outside the clear-sign list are blind; "air-gapped" holds only if the
  signer ROM has no Wi-Fi code at all.
- First step: a one-day spike reading a static version-10 QR off a MetaMask
  screen with the DSi camera.

## B. Light client so the console checks inclusion itself: possible, later

- Chain of proof: a trusted checkpoint, `LightClientBootstrap` (current sync
  committee, Merkle branch into the state), updates whose sync-aggregate BLS
  signature covers the attested header (finality branch to the finalized
  one), the execution header proved into the beacon body (it gives the EL
  block hash), then the EL headers the console already re-hashes, back by
  parent hash to the block, and the transactions trie it already rebuilds.
- BLS12-381 with blst (Apache-2.0, portable C): a pairing check is ~125 M
  cycles in the model, a whole update ~202 M (subtracting the few
  non-participants from the committee's aggregate key instead of adding 512
  keys): 5-6 s on the DSi as calibrated, 1-2 s with the field arithmetic in
  ITCM and an assembly Montgomery multiply (estimate). 12.6 KB peak stack,
  66 KB of code. One update per ~27 h sync period, plus one per check.
- Getting the data without an RPC: the Portal beacon network is no longer
  maintained (its spec moved to legacy in 2025); consensus libp2p over TCP
  (Noise, multistream, mplex/yamux, the light_client req/resp protocols) is
  the way: about 2,200 lines with SSZ, plus discv5 or a known peer.
- Risks: public consensus peers drop peers that do not gossip; forks move the
  Merkle indices; a long time offline means many updates to catch up.

## Recommendation

Build A first: it works with MetaMask today, reuses the signing already
measured on the DSi (30 ms) and qrcodegen, and needs no peers. Start B with its
verification core on a computer against the consensus-spec test vectors, and
run the timing ROM on the DSi before deciding on the transport.
