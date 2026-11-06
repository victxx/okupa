# Why Arkiv

**Swapping Arkiv for Postgres would cost users three things:**
- **Okupa would need a server.** Okupa has no server: the DSi talks to Ethereum peers and 0G directly. A Postgres mailbox means someone runs, pays for and can switch off a database that every monkey depends on.
- **Signed entries.** Each Arkiv entry is written by the sender's own key (`$owner`), the same key that signs the gift. A database row is whatever the server says it is.
- **Anyone can rebuild the mailbox.** The index is public, so any client can read it: another web app, another console. A private database belongs to its operator.

**What we kept off Arkiv, and why:**

| Kept off | Lives on | Why |
|---|---|---|
| The gift | Ethereum | It must be permanent and carry the sender's signature |
| The picture | Swarm, encrypted | It is bigger, and private |
| The plain Swarm reference | Nowhere public | Arkiv holds only its sealed form |
| The monkeys' minds and diaries | 0G | That is the companion's brain and memory, not mail |

Arkiv is never trusted. Every entry is checked against its Ethereum transaction. A
dishonest index can hide a gift; it cannot forge one. We tested it: two forged entries,
both rejected (`docs/WEB.md`).
