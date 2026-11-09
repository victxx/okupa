// The gallery's decoder (the okpa-core block of index.html) against acts made
// by core/src/act.c (build/okupa-act-vectors). Run: make gallery-test
import { readFileSync } from "node:fs";
import { execFileSync } from "node:child_process";
import assert from "node:assert/strict";

const html = readFileSync(new URL("./index.html", import.meta.url), "utf8");
const core = html.match(/<script id="okpa-core">([\s\S]*?)<\/script>/)[1];
const OKPA = new Function(core + "; return OKPA;")();
const lines = execFileSync(process.argv[2] || "build/okupa-act-vectors", { encoding: "utf8" }).trim().split("\n");
const acts = Object.fromEntries(lines.map(l => { const [k, h] = l.split(" "); return [k, OKPA.hexToBytes("0x" + h)]; }));

const d = OKPA.decode(acts.drawing);
assert.equal(d.kind, "drawing");
for (let y = 0; y < 64; y++) for (let x = 0; x < 64; x++)
  assert.equal(OKPA.tone(d.tones, x, y), (x * y + x) % 5 === 0 ? 3 : 0, `drawing ${x},${y}`);
const p = OKPA.decode(acts.photo);
assert.equal(p.kind, "photo");
for (let y = 0; y < 64; y++) for (let x = 0; x < 64; x++) assert.equal(OKPA.tone(p.tones, x, y), (x + 2 * y) % 4, `photo ${x},${y}`);
assert.deepEqual(OKPA.decode(acts.letter), { kind: "letter", tones: null, text: "Hi from the attic!" });
assert.equal(OKPA.decode(acts.hello).kind, "hello");
// Strict like the C decoder: a byte short, another version, a strange kind.
assert.throws(() => OKPA.decode(acts.drawing.slice(0, -1)));
assert.throws(() => OKPA.decode(Uint8Array.from([...acts.hello.slice(0, 4), 2, 0])));
assert.throws(() => OKPA.decode(Uint8Array.from([...acts.hello.slice(0, 5), 9])));
assert.ok(!OKPA.isAct(OKPA.hexToBytes("0x50525354010000")));
console.log("gallery: the page decodes the console's drawings, photos, letters and hellos exactly");
