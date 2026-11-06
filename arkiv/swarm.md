# Arkiv + Swarm

| Lives on | What | Why there |
|---|---|---|
| Swarm | The picture: 64×64, four tones, 1030 B, uploaded with `Swarm-Encrypt: true` | Content-addressed and censorship-resistant storage. The picture can be bigger than a transaction should carry |
| Arkiv | The mailbox entry, with the **sealed** Swarm reference in the `sealed` field | Queryable by recipient without scanning a chain; signed by the sender |
| Ethereum | The gift itself: `OKPA 01 05` + the same sealed reference | The permanent record of who gave what to whom |

The prize page warns that an encrypted Swarm reference contains its key and that Arkiv
entities are public. So the reference never appears in the clear:
- It is sealed with ECIES to the recipient's public key: `ref (64 B) ‖ keccak256(picture)`, with the header and recipient address bound in the MAC.
- Only the recipient can open it.
- A gateway can withhold the picture but cannot swap it, because the keccak is checked.

| Item | Value |
|---|---|
| Arkiv entity → Swarm chunk | TBD after the public run (local run: entity `0xd7787e35…6329` → encrypted root chunk `5c310842…0031`) |
| Postage batch | TBD (local run: `458ec8cb…309e`, bought by a Bee 2.8.0 dev node) |
| Bee node | TBD (local run: `http://127.0.0.1:1633`) |
| Upload / read code | [`web/src/swarm.js`](../web/src/swarm.js); `sendSwarmGift` and `checkMailbox` in [`web/src/app.js`](../web/src/app.js) |
| Video timestamp | TBD |

**Run one upload and one read yourself.** Follow [`web/README.md`](../web/README.md):
1. Start two browser monkeys.
2. A draws and sends **Gift via Swarm + Arkiv**.
3. B presses **Check mailbox**.

You should see:
- the gift found in Arkiv;
- the sender recovered from the Ethereum signature;
- the seal opened;
- 1030 bytes from Swarm matching the sealed hash;
- the picture on B's wall.

**What got in the way**
- `bee dev` was removed after Bee 2.8.0, so the local stack pins 2.8.0.
- Uploading to the public network needs a funded Bee node and a postage batch.
