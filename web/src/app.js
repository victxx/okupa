// Okupa web: a browser monkey that trades pictures with other monkeys.
// Ethereum carries the signed acts, Swarm the (encrypted) pictures, Arkiv an untrusted mailbox.
import { secp256k1 } from '@noble/curves/secp256k1.js';
import { keccak_256 } from '@noble/hashes/sha3.js';
import { privateKeyToAccount } from 'viem/accounts';
import { formatEther, formatGwei, createPublicClient, http } from 'viem';
import { NETWORKS } from './config.js';
import { bytesToHex, hexToBytes, equal } from './bytes.js';
import * as okupa from './okupa.js';
import { publicKey } from './ecies.js';
import { seal, open, checkPicture } from './seal.js';
import * as swarm from './swarm.js';
import * as arkiv from './arkiv.js';
import { clients, sendAct, quoteAct, waitIncluded, verifiedAct } from './chain.js';
import { ANON, anonFetch } from './anon.js';

const $ = (id) => document.getElementById(id);
const store = {
  get: (k, d = null) => { try { const v = localStorage.getItem('okupa.web.' + k); return v == null ? d : JSON.parse(v); } catch { return d; } },
  set: (k, v) => { try { localStorage.setItem('okupa.web.' + k, JSON.stringify(v)); } catch { /* private mode */ } },
};

// ---------- the monkey's key: made here, kept here ----------
let priv = store.get('key');
if (!priv) { priv = '0x' + bytesToHex(secp256k1.utils.randomSecretKey()); store.set('key', priv); }
const account = privateKeyToAccount(priv);
const me = account.address.toLowerCase();
const myPub = publicKey(hexToBytes(priv));
const friendCode = `${account.address}:${bytesToHex(myPub)}`;

let netId = store.get('net', 'local');
if (!NETWORKS[netId] || NETWORKS[netId].readOnly) netId = 'local';
const net = () => NETWORKS[netId];

// ---------- log: only what really happened ----------
const evidence = store.get('evidence', []);
function log(msg, cls = '') {
  const li = document.createElement('li');
  li.textContent = `${new Date().toLocaleTimeString()}  ${msg}`;
  if (cls) li.className = cls;
  $('log').prepend(li);
}
function record(e) { evidence.push({ at: new Date().toISOString(), net: netId, ...e }); store.set('evidence', evidence); }
window.okupaEvidence = () => evidence;

// ---------- sketchbook: 64x64, four tones ----------
const TONES = ['--t0', '--t1', '--t2', '--t3'].map((v) => getComputedStyle(document.documentElement).getPropertyValue(v).trim());
let pixels = store.get('draft') ? hexToBytes(store.get('draft')) : new Uint8Array(okupa.PHOTO_BYTES);
let tone = 3;

function paint(canvas, px, bpp) {
  const s = canvas.width / okupa.SIDE, g = canvas.getContext('2d');
  for (let y = 0; y < okupa.SIDE; y++) for (let x = 0; x < okupa.SIDE; x++) {
    const v = okupa.getPixel(px, bpp, x, y);
    g.fillStyle = TONES[bpp === 1 ? (v ? 3 : 0) : v];
    g.fillRect(x * s, y * s, s, s);
  }
}
function drawAt(ev) {
  const c = $('sketch'), r = c.getBoundingClientRect();
  const x = Math.floor(((ev.clientX - r.left) / r.width) * okupa.SIDE), y = Math.floor(((ev.clientY - r.top) / r.height) * okupa.SIDE);
  if (x < 0 || y < 0 || x >= okupa.SIDE || y >= okupa.SIDE) return;
  for (const [dx, dy] of [[0, 0], [1, 0], [0, 1], [1, 1]]) if (x + dx < okupa.SIDE && y + dy < okupa.SIDE) okupa.setPixel(pixels, 2, x + dx, y + dy, tone);
  paint(c, pixels, 2);
}
function setupSketch() {
  const c = $('sketch');
  let down = false;
  c.addEventListener('pointerdown', (e) => { down = true; c.setPointerCapture(e.pointerId); drawAt(e); });
  c.addEventListener('pointermove', (e) => { if (down) drawAt(e); });
  c.addEventListener('pointerup', () => { down = false; store.set('draft', bytesToHex(pixels)); });
  TONES.forEach((t, i) => {
    const b = document.createElement('button');
    b.className = 'tone'; b.style.background = t; b.setAttribute('aria-label', `Tone ${i}`);
    b.setAttribute('aria-pressed', String(i === tone));
    b.onclick = () => { tone = i; document.querySelectorAll('.tone').forEach((x, j) => x.setAttribute('aria-pressed', String(j === i))); };
    $('tones').append(b);
  });
  $('clear').onclick = () => { if (confirm('Start a new page? The current drawing is lost.')) { pixels = new Uint8Array(okupa.PHOTO_BYTES); store.set('draft', bytesToHex(pixels)); paint(c, pixels, 2); } };
  paint(c, pixels, 2);
}

// ---------- friends ----------
function parseFriend(code) {
  const m = code.trim().match(/^(0x[0-9a-fA-F]{40})(?::(?:0x|04)?([0-9a-fA-F]{128}))?$/);
  if (!m) throw new Error('friend code is 0xADDRESS:PUBLICKEY (or just 0xADDRESS for an inline gift)');
  const address = m[1].toLowerCase();
  if (!m[2]) return { address, pub: null };
  const pub = hexToBytes(m[2]);
  const derived = '0x' + bytesToHex(keccak_256(pub).subarray(12));
  if (derived !== address) throw new Error('that public key does not belong to that address');
  secp256k1.Point.fromBytes(new Uint8Array([4, ...pub])); // throws if not on the curve
  return { address, pub };
}

// ---------- wall ----------
const wallKey = () => `wall.${net().chainId}`;
function frame(px, bpp, lines) {
  const f = document.createElement('div'); f.className = 'frame';
  const c = document.createElement('canvas'); c.width = c.height = 128; paint(c, px, bpp);
  f.append(c);
  for (const l of lines) { const p = document.createElement('span'); p.className = 'mono'; p.textContent = l; f.append(p); }
  return f;
}
function renderWall() {
  const w = $('wall'); w.replaceChildren();
  const hung = store.get(wallKey(), []);
  if (!hung.length) { w.textContent = 'Nothing hung yet.'; w.className = 'wall muted'; return; }
  w.className = 'wall';
  for (const h of hung) w.append(frame(hexToBytes(h.pixels), h.bpp, [`from ${h.from.slice(0, 10)}…`, `${h.how}`, `tx ${h.tx.slice(0, 12)}…`]));
}

// ---------- network ----------
async function refreshBalance() {
  const n = net();
  try {
    const { pub } = clients(n);
    const eth = await pub.getBalance({ address: me });
    let line = `${n.name}: ${formatEther(eth)} ETH`;
    if (n.arkiv) {
      const a = createPublicClient({ chain: arkiv.arkivChain(n.arkiv), transport: http(n.arkiv.rpc) });
      line += ` · ${n.arkiv.name}: ${formatEther(await a.getBalance({ address: me }))} ${n.arkiv.symbol}`;
    }
    $('balance').textContent = line;
  } catch (e) { $('balance').textContent = `${n.name}: unreachable (${e.shortMessage || e.message})`; }
}

function setupNet() {
  for (const sel of [$('net'), $('visitNet')]) {
    for (const [id, n] of Object.entries(NETWORKS)) {
      if (sel === $('net') && n.readOnly) continue;
      const o = document.createElement('option'); o.value = id; o.textContent = n.name; sel.append(o);
    }
  }
  $('net').value = netId;
  $('visitNet').value = store.get('visitNet', 'sepolia');
  for (const [id, a] of [['', { name: 'direct RPC reads' }], ...Object.entries(ANON)]) {
    const o = document.createElement('option'); o.value = id; o.textContent = a.name; $('visitVia').append(o);
  }
  $('net').onchange = () => { netId = $('net').value; store.set('net', netId); showNet(); };
  showNet();
}
function showNet() { if ($('fund')) $('fund').hidden = !net().devFaucet; $('netlabel').textContent = net().label + ' A browser cannot open TCP, so this monkey reads and sends through JSON-RPC (the DSi talks devp2p directly).'; renderWall(); refreshBalance(); }

// ---------- sending ----------
async function busy(btn, fn) {
  const all = document.querySelectorAll('button'); all.forEach((b) => (b.disabled = true));
  try { await fn(); } catch (e) { log(`✗ ${e.shortMessage || e.message}`, 'bad'); console.error(e); } finally { all.forEach((b) => (b.disabled = false)); refreshBalance(); }
}

// On a network with real money, say what the act costs and ask before anything is spent.
// The quote is an upper bound: the limit assumes every calldata byte is non-zero.
async function quoteOrAsk(n, size, what) {
  const q = await quoteAct(n, me, new Uint8Array(size).fill(0xff));
  if (n.confirmSpend) {
    const msg = `${what} on ${n.name} (real money).\n\nGas limit ${q.gas}, base fee ${formatGwei(q.base)} gwei, tip ${formatGwei(q.tip)} gwei.\nExpected about ${formatEther(q.expectedWei)} ETH, at most ${formatEther(q.maxWei)} ETH.\n\nSign and send?`;
    if (!confirm(msg)) throw new Error('not sent: you said no');
    log(`${n.name}: you approved up to ${formatEther(q.maxWei)} ETH for this act.`, 'dim');
  }
  return q;
}

async function sendSwarmGift() {
  const n = net(), friend = parseFriend($('to').value);
  if (!friend.pub) throw new Error('a Swarm gift needs the full friend code (address and public key)');
  const picture = okupa.photo(pixels);
  const q = await quoteOrAsk(n, 6 + okupa.SEALED_REF_BYTES, 'A Swarm gift: one transaction');
  log(`Picture: 64×64, four tones, ${picture.length} B (OKPA 01 02).`, 'dim');
  const batch = await swarm.postageBatch(n.bee);
  const ref = await swarm.uploadEncrypted(n.bee, picture, batch);
  log(`Swarm: uploaded encrypted through ${n.bee}, batch ${batch.slice(0, 10)}…. Chunk address ${bytesToHex(ref.subarray(0, 32)).slice(0, 16)}…; its key half stays secret.`, 'ok');
  const sealed = seal(friend.pub, friend.address, ref, picture);
  const data = okupa.giftSwarm(sealed);
  log(`Sealed the reference to ${friend.address.slice(0, 10)}… with ECIES: ${sealed.length} B. Calldata ${data.length} B (OKPA 01 05).`, 'dim');
  const { hash, nonce } = await sendAct(n, priv, friend.address, data, q);
  log(`Ethereum: signed here, sent through RPC. tx ${hash} (nonce ${nonce}).`, 'ok');
  const { block, status } = await waitIncluded(n, hash);
  log(`Ethereum: included in block ${block} (${status}), as the RPC reports it.`, status === 'success' ? 'ok' : 'bad');
  const a = await arkiv.postGift(n.arkiv, priv, { to: friend.address, kind: okupa.KIND.GIFT_SWARM, chainId: n.chainId, tx: hash, block, sealed });
  log(`Arkiv: mailbox entry ${a.entityKey} (Arkiv tx ${a.txHash}).`, 'ok');
  record({ act: 'gift-swarm', from: me, to: friend.address, tx: hash, block: String(block), swarmUpload: 'encrypted', sealedBytes: sealed.length, arkivEntity: a.entityKey, arkivTx: a.txHash, bee: n.bee, postageBatch: batch, swarmAddress: bytesToHex(ref.subarray(0, 32)), swarmKey: 'withheld (sealed to the recipient)' });
}

async function sendInlineGift() {
  const n = net(), friend = parseFriend($('to').value);
  const drawing = okupa.toDrawing(pixels);
  const data = okupa.giftPicture(drawing);
  log(`Inline gift: 64×64 one-bit drawing in the calldata, ${data.length} B (OKPA 01 03), the DSi's own format.`, 'dim');
  const q = await quoteOrAsk(n, data.length, 'An inline gift: one transaction');
  const { hash, nonce } = await sendAct(n, priv, friend.address, data, q);
  log(`Ethereum: tx ${hash} (nonce ${nonce}).`, 'ok');
  const { block, status } = await waitIncluded(n, hash);
  log(`Ethereum: included in block ${block} (${status}).`, status === 'success' ? 'ok' : 'bad');
  let entity = null;
  if (n.arkiv) {
    const a = await arkiv.postGift(n.arkiv, priv, { to: friend.address, kind: okupa.KIND.GIFT_PICTURE, chainId: n.chainId, tx: hash, block, sealed: new Uint8Array(0) });
    entity = a.entityKey;
    log(`Arkiv: mailbox entry ${a.entityKey} (a web monkey finds it there; a DSi would find it on chain).`, 'ok');
  }
  record({ act: 'gift-inline', from: me, to: friend.address, tx: hash, block: String(block), arkivEntity: entity });
}

// ---------- receiving ----------
async function checkMailbox() {
  const n = net(), list = $('mail');
  list.replaceChildren();
  const entries = await arkiv.giftsFor(n.arkiv, me, n.chainId);
  log(`Arkiv: ${entries.length} mailbox entr${entries.length === 1 ? 'y' : 'ies'} name this monkey. Checking each against the chain.`, 'dim');
  const hung = store.get(wallKey(), []);
  for (const e of entries) {
    const li = document.createElement('li'); li.className = 'mono'; list.append(li);
    const say = (t) => { li.textContent = `${e.key.slice(0, 12)}… ${t}`; };
    try {
      if (e.owner !== e.from) throw new Error('entry not written by the sender it names');
      const v = await verifiedAct(n, e.tx);
      if (!v.ok) throw new Error(`transaction: ${v.why}`);
      if (v.from !== e.from) throw new Error('transaction sender differs from the entry');
      if (v.to !== me) throw new Error('transaction is not addressed to this monkey');
      if (v.act.kind !== e.kind) throw new Error('kind differs from the entry');
      if (v.act.kind === okupa.KIND.GIFT_SWARM && !equal(v.act.sealed, e.sealed)) throw new Error('sealed bytes differ from the transaction');
      if (hung.some((h) => h.tx === e.tx)) { say('checked, already on the wall'); continue; }
      let px, bpp, how;
      if (v.act.kind === okupa.KIND.GIFT_SWARM) {
        const { ref, pictureHash } = open(hexToBytes(priv), me, v.act.sealed);
        const picture = await swarm.download(n.bee, ref);
        if (!checkPicture(picture, pictureHash)) throw new Error('Swarm returned other bytes than the sender sealed');
        const p = okupa.decode(picture);
        if (!p || p.kind !== okupa.KIND.PHOTO) throw new Error('not a picture');
        px = p.pixels; bpp = 2; how = 'via Swarm';
        log(`✓ ${e.tx.slice(0, 12)}…: sender ${v.from.slice(0, 10)}… recovered from the signature, block ${v.block}, seal opened, ${picture.length} B from Swarm match the sealed keccak.`, 'ok');
        record({ act: 'received-swarm', from: v.from, to: me, tx: e.tx, block: String(v.block), arkivEntity: e.key, swarmAddress: bytesToHex(ref.subarray(0, 32)), pictureKeccak: bytesToHex(pictureHash), pictureBytes: picture.length });
      } else if (v.act.kind === okupa.KIND.GIFT_PICTURE) {
        px = v.act.pixels; bpp = v.act.bpp; how = 'inline';
        log(`✓ ${e.tx.slice(0, 12)}…: inline picture from ${v.from.slice(0, 10)}…, block ${v.block}.`, 'ok');
        record({ act: 'received-inline', from: v.from, to: me, tx: e.tx, block: String(v.block), arkivEntity: e.key });
      } else throw new Error('unknown gift kind');
      hung.unshift({ tx: e.tx, from: v.from, pixels: bytesToHex(px), bpp, how });
      store.set(wallKey(), hung.slice(0, 12));
      say(`hung (${how})`);
    } catch (err) {
      say(`rejected: ${err.message}`);
      log(`✗ Arkiv entry ${e.key.slice(0, 12)}… rejected: ${err.message}`, 'bad');
    }
  }
  renderWall();
}

// ---------- visiting a monkey (e.g. a DSi) ----------
async function findActs(n, address, f = fetch) {
  if (n.blockscout) {
    const r = await f(`${n.blockscout}/api/v2/addresses/${address}/transactions`);
    if (!r.ok) throw new Error(`explorer: ${r.status}`);
    const { items } = await r.json();
    return (items || []).map((t) => t.hash);
  }
  const { pub } = clients(n);
  const head = await pub.getBlockNumber(), hashes = [];
  for (let b = head; b >= 0n && b > head - BigInt(n.scanBlocks); b--) {
    const blk = await pub.getBlock({ blockNumber: b, includeTransactions: true });
    for (const t of blk.transactions) if (t.from.toLowerCase() === address || t.to?.toLowerCase() === address) hashes.push(t.hash);
  }
  return hashes;
}

async function visit() {
  const address = $('visitAddr').value.trim().toLowerCase();
  if (!/^0x[0-9a-f]{40}$/.test(address)) throw new Error('enter an address');
  const id = $('visitNet').value, n = NETWORKS[id];
  store.set('visitNet', id);
  const out = $('visitOut'); out.replaceChildren(); out.className = 'wall';
  const via = n.blockscout ? anonFetch($('visitVia').value) : null;
  if (via) { log(`Reads go through ${via.name}; waiting for its sandboxed worker…`, 'dim'); await via.ready; log(`${via.name}: hash-verified worker ready.`, 'ok'); }
  const hashes = await findActs(n, address, via?.fetch);
  log(`Visit ${address.slice(0, 10)}… on ${n.name}: ${hashes.length} transaction(s) to check${via ? ', via ' + via.name : ', direct'}.`, 'dim');
  let hellos = 0, shown = 0;
  for (const h of hashes) {
    let v;
    try { v = await verifiedAct(n, h, via?.fetch); } catch (err) { log(`  ${h.slice(0, 12)}…: skipped (${err.shortMessage || err.message})`, 'dim'); continue; }
    if (!v.ok) continue;
    if (v.from !== address && v.to !== address) continue;
    const self = v.from === v.to;
    if (v.act.kind === okupa.KIND.HELLO && self) { hellos++; continue; }
    if ((v.act.kind === okupa.KIND.DRAWING || v.act.kind === okupa.KIND.PHOTO) && self) out.append(frame(v.act.pixels, v.act.bpp, ['drawing', `block ${v.block}`, `tx ${h.slice(0, 12)}…`]));
    else if (v.act.kind === okupa.KIND.GIFT_PICTURE) out.append(frame(v.act.pixels, v.act.bpp, [v.from === address ? `gift to ${v.to.slice(0, 10)}…` : `gift from ${v.from.slice(0, 10)}…`, `block ${v.block}`, `tx ${h.slice(0, 12)}…`]));
    else if (v.act.kind === okupa.KIND.GIFT_SWARM) { const p = document.createElement('p'); p.className = 'muted mono'; p.textContent = `sealed Swarm gift ${v.from === address ? 'to ' + v.to : 'from ' + v.from} (only the recipient can open it) · tx ${h.slice(0, 12)}…`; out.append(p); }
    else continue;
    shown++;
  }
  const p = document.createElement('p'); p.className = 'muted';
  p.textContent = `${hellos} hello(s) and ${shown} picture act(s), each checked: signature recovers the sender, calldata is an Okupa act.${hashes.length ? '' : ' Nothing found.'}`;
  out.prepend(p);
  record({ act: 'visit', address, network: id, via: via?.name ?? 'direct RPC', transactions: hashes.length, hellos, pictures: shown });
}

// ---------- dev conveniences (local stack only) ----------
async function devFund() {
  const n = net();
  if (!n.devFaucet) return;
  const rpc = (url, method, params) => fetch(url, { method: 'POST', headers: { 'content-type': 'application/json' }, body: JSON.stringify({ jsonrpc: '2.0', id: 1, method, params }) })
    .then((r) => r.json()).then((j) => { if (j.error) throw new Error(j.error.message); return j.result; });
  for (const [name, url] of [[n.name + ' chain', n.rpc], [n.arkiv.name, n.arkiv.rpc]]) {
    const [dev] = await rpc(url, 'eth_accounts', []);
    const tx = await rpc(url, 'eth_sendTransaction', [{ from: dev, to: me, value: '0xde0b6b3a7640000' }]);
    log(`${name}: its unlocked dev account sent this monkey 1 coin (tx ${tx.slice(0, 12)}…). Local dev convenience only.`, 'dim');
  }
  await new Promise((r) => setTimeout(r, 3000));
}

// ---------- boot ----------
$('addr').textContent = account.address;
$('mycode').value = friendCode;
$('mycode').onclick = () => { $('mycode').select(); navigator.clipboard?.writeText(friendCode).catch(() => {}); };
setupSketch();
setupNet();
$('sendSwarm').onclick = (e) => busy(e.target, sendSwarmGift);
$('sendInline').onclick = (e) => busy(e.target, sendInlineGift);
$('check').onclick = (e) => busy(e.target, checkMailbox);
$('visit').onclick = (e) => busy(e.target, visit);
$('fund').onclick = (e) => busy(e.target, devFund);
$('fund').hidden = !net().devFaucet;
log(`This monkey is ${account.address}. Its key never leaves this browser.`, 'dim');
