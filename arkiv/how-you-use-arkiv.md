# How Okupa uses Arkiv

| Arkiv feature | Use | What the user sees |
|---|---|---|
| Indexed `to` + `chain` | "Gifts for me" without scanning the chain | **Check** fills the wall (small gifts) and **the cave** (big photos) |
| Indexed `from`, `kind` | Possible later: filter by friend or by gift type | Not used yet |
| `$owner` (chain-set) | Must equal `from`, so a third party cannot post in someone's name | Forged entries show as "rejected" in the mailbox |
| Expiry, 7 days | A mailbox is a notification; the gift stays on Ethereum | Old entries disappear, the picture stays on the wall |
| `bytes` column | Carries the sealed Swarm reference | The picture opens only for its recipient |

**Who can edit what:**
- Only the sender's key can create, change or delete its entries.
- The recipient never writes to someone else's entry.
- The page treats every entry as untrusted and checks the Ethereum transaction (signature, recipient, calldata) before showing anything.
