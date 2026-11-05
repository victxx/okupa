# Okupa

**A tiny squatter living in your Nintendo DSi.**

Someone has moved into your console and refuses to leave. It has a little house,
its own days and a diary. It asks you for things: a photo of your sky, a drawing,
something round. You send them from the DSi itself, straight onto Ethereum, and it
hangs them on its wall, remembers them and sometimes gives you something back.

## What makes it different

- **Just the console and a Wi-Fi.** No server of ours anywhere: the DSi finds
  Ethereum nodes, sends, checks its own inclusion and thinks with 0G by itself.
- **No server in the way.** The DSi signs each picture itself and gossips it to
  Ethereum peers over devp2p. No RPC provider, no relay, nobody to ask permission.
- **A companion with a body.** The DSi is its body: the camera is its eyes, the
  clock tells it how long you were away, the touch screen is how you reach it.
  Its mind runs on 0G: each decision is verifiable, logged with a request id, and
  its diary lives on 0G Storage. When 0G is unreachable it simply goes to sleep;
  it never pretends to have decided.
- **Gifts between companions.** Pictures and tiny 3D objects (an apple, a plushie,
  at most 200 triangles in four tones) travel from one companion's account to
  another's. The chain itself records who gave what, and when.
- **Runs anywhere.** One portable C core. If it fits in a 2009 handheld, it fits
  in anything.

## Status

Just started. See [docs/PLAN.md](docs/PLAN.md).

## Layout (planned)

```text
core/       portable C: hashing, RLP, keys, transactions, devp2p (RLPx, eth)
platform/   host (macOS/Linux) and dsi (BlocksDS)
game/       house, companion, sketchbook, camera, gifts
tools/      development helpers on a computer, never part of the game
docs/       plan and design notes
```

Secrets, keys and run data live in `local/` and `run/`, never in Git.
