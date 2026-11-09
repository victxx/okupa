# Audit, 2026-11-07

Two read-only reviews of `main`: one for bugs (network and core), one for the
implicit state machines across the game, the postman, the mind and the
session. `make test` and `make sanitize` pass. No memory-safety bugs were
found in the RLP, snappy, ECIES, RLPx, devp2p or discv4 decoders.

## Fixed (afabe53)

- **The account key could be overwritten.** Making a new key after the
  wallet failed to open replaced a funded `account.key`. Now an existing
  key file is never replaced; it is loaded instead.
- **A nonce was used up before signing.** A failed signature (for example,
  a letter before the key was loaded) left a gap that blocked every later
  transaction. Now the nonce is read, the transaction signed, and only then
  is the nonce committed. The nonce file is written to a temporary file and
  renamed.
- **The wallet only opened from Dev > Wallet.** It now opens at boot.
- **A drawing could stay "on its way" forever.** This happened when it was
  included while the SELECT screens were open and the power went off. It
  now hangs at once. At boot, a drawing still waiting with nothing pending
  is hung if `included-*.txt` lists it; otherwise its page is released.
  `drawing-sent.bin` is written before the transaction is handed to the
  postman.
- **Block watcher:**
  - a body request now has its own timeout;
  - a body whose trie root does not match its header is asked for again
    (up to three times) instead of being skipped;
  - `watch_from` only advances on a matching root.
- **Held sessions** stay open only when they were dialled with the
  transaction they are watching for.
- **Holding R for the fps overlay also took a camera photo for 0G.** The
  overlay is now on START.
- **False "STUCK" during Wi-Fi scans and connects.** `show_now` now counts
  as a heartbeat.

## Done since (2026-11-09)

- **A pending transaction has a way out:** Speed up re-signs it with the same
  nonce and twice the fee (three times at most), saved first; any version a
  block holds counts.
- **Inclusion no longer trusts one peer:** a second, different node must serve
  the same block with the transaction in it.
- **Mind requests with no consumer:** they fail at once when the mind is not
  running; and the okupa has its own small mind without 0G.
- **Kind labels:** letters and hellos are gone; the snapshot keeps the kind
  of picture on its way.
- **`wall.bin` not atomic:** the wall is in the two-file snapshot now.

## Still open

1. **Inclusion is not consensus:** two peers agreeing is better than one, but
   only a light client would check finality (docs/RESEARCH.md).
2. **Watching only happens while a node holds us**, and admission still
   decides everything on mainnet. The backlog after a long time off is not
   capped.
3. **Fees are not read from headers:** the first fee is fixed (2 gwei max);
   Speed up is the answer when the base fee climbs.
4. **Keys in plain text on the SD** (account.key, mind.key, wifi.txt).
5. **Smaller items:** L, R and X act during the intro; the SELECT screens
   skip the save while open; peers that went full still count as ready.

## Open on 2026-11-07, by priority


1. **A pending transaction has no way out.** There is no expiry, no
   replace-by-fee and no "forget" action in Dev, so a dropped or
   underpriced transaction blocks sending. The max fee is fixed at 2 gwei.
2. **Inclusion trusts one peer.** A peer that knows our hash could forge a
   header and a matching body. Each batch should be linked to the previous
   one, and a second peer should agree before pending is cleared.
3. **Watching only happens while a node holds us.** The DSi's own mainnet
   run reached Hello with 7 of 25 nodes but Status with none, so on the
   console a transaction may be handed to few or no nodes.
   - Admission still decides everything on mainnet.
   - Waking up after a long time off fetches every body since `watch_from`.
     That backlog should be capped.
4. **Mind requests with no consumer.** Without `mind.txt`, or after a setup
   failure, pressing L or X leaves "Thinking…" or a Pictionary round waiting
   forever. Gate them on the mind running and fail them when it stops.
5. **Kind labels.** `tx_kind` is not saved, so after a reboot a letter's
   inclusion can read "hello", and Mail's "still on its way" also covers
   drawings.
6. **Smaller items:**
   - L, R and X act during the intro.
   - The SELECT screens skip the release and the save while open.
   - `wall.bin` is not written atomically.
   - Peers that went full are still counted as "ready".
   - The mind's "sleep" can be refused while it still yawns.
