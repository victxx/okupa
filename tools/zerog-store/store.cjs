// Store one okupa decision episode on 0G Storage (Galileo testnet), then read
// it back by root with the Merkle proof and check it is byte for byte the same.
// Prints one JSON line: root, tx, chat id and stage of the episode.
//   node tools/zerog-store/store.cjs <episode.json> <0G testnet key file>
// The key pays the storage fee on testnet; it is never printed.
const { readFileSync, mkdtempSync, rmSync } = require("node:fs");
const { join } = require("node:path");
const { tmpdir } = require("node:os");
const { Indexer, MemData } = require("@0gfoundation/0g-storage-ts-sdk");
const { ethers } = require("ethers");

const RPC = "https://evmrpc-testnet.0g.ai";
const INDEXER = "https://indexer-storage-testnet-turbo.0g.ai";

(async () => {
  const [file, keyFile] = process.argv.slice(2);
  if (!file || !keyFile) throw new Error("usage: store.cjs <episode.json> <key file>");
  const bytes = readFileSync(file);
  const episode = JSON.parse(bytes.toString("utf8"));
  if (episode.okupa_episode !== 1) throw new Error("not an okupa episode");
  const signer = new ethers.Wallet(readFileSync(keyFile, "utf8").trim(), new ethers.JsonRpcProvider(RPC));
  const data = new MemData(bytes);
  const [tree, treeError] = await data.merkleTree();
  if (treeError !== null || !tree) throw new Error(`merkle tree: ${treeError}`);
  const indexer = new Indexer(INDEXER);
  const [tx, error] = await indexer.upload(data, RPC, signer);
  if (error !== null) throw new Error(`upload: ${error}`);
  if (!("rootHash" in tx)) throw new Error("an episode must not be fragmented");
  if (tx.rootHash !== tree.rootHash()) throw new Error("uploaded root differs from the local tree");

  // Read it back with the proof: the stored episode is the one we made.
  const dir = mkdtempSync(join(tmpdir(), "okupa-episode-"));
  let same = false;
  try {
    const back = join(dir, "episode.json");
    for (let i = 0; i < 12 && !same; i++) {
      const err = await indexer.download(tx.rootHash, back, true);
      if (err === null) same = Buffer.compare(readFileSync(back), bytes) === 0;
      else await new Promise((r) => setTimeout(r, 5000)); // nodes may still be syncing the file
    }
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
  console.log(JSON.stringify({ root: tx.rootHash, tx: tx.txHash, chat_id: episode.chat_id, stage: episode.stage,
    verified: episode.verified, bytes: bytes.length, read_back_identical: same, file }));
  if (!same) process.exit(1);
})().catch((e) => { console.error("store:", e.message); process.exit(1); });
