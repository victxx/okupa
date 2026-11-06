# Arkiv schema

One entity type, `okupa-gift-v1`: one entry per gift between two monkeys. It is a mailbox
that tells the recipient where to look. The gift itself is an Ethereum transaction.
Code: [`web/src/arkiv.js`](../web/src/arkiv.js).

| Field | Type | Indexed | Holds |
|---|---|---|---|
| `to` | addr | yes | Recipient monkey (its Ethereum address) |
| `from` | addr | yes | Sender monkey. It must equal `$owner` and the transaction's recovered sender, or the entry is rejected |
| `kind` | u64 | yes | Okupa act kind: `3` inline picture, `5` picture on Swarm |
| `chain` | u64 | yes | Ethereum chain id of the gift transaction (1 mainnet, 11155111 Sepolia) |
| `tx` | str | no | Gift transaction hash |
| `block` | u64 | no | Block the sender saw it in (a hint) |
| `sealed` | bytes | no | For kind 5: the Swarm reference sealed to the recipient (209 B, ECIES), the same bytes as in the transaction. Empty for kind 3 |

| Question | Answer |
|---|---|
| Owner | The sender's own key, the same secp256k1 key that signs the Ethereum gift. Nobody else can edit or delete an entry |
| Expiry | 7 days. It is a notification; the permanent record is the Ethereum transaction |
| Queried fields | `to` and `chain`: "gifts for me on this chain" |
| Created | Once per gift, right after the gift transaction is included |

Query judges can run:

```text
arkiv.schema = str('okupa-gift-v1') AND to = addr(<monkey address>) AND chain = u64(1)
```

Or:

```bash
node web/scripts/read-mailbox.mjs mainnet <address>
```
