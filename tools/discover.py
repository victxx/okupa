"""Ethereum's "yellow pages", step by step: find Sepolia nodes without scanning.

Two ways in, both public and both opt-in on the nodes' side:

  DNS (EIP-1459)   Node lists signed and published as DNS TXT records.
                   python tools/discover.py --dns
                   Same thing by hand:  dig +short TXT all.sepolia.ethdisco.net

  discv4 (UDP)     The nodes' own table: start at a bootnode, ask "who do you
                   know?", then ask those nodes for their signed record (ENR).
                   python tools/discover.py
                   python tools/discover.py --nodes 20 --bootnode 2

Either way, a node marked SEPOLIA is only a candidate: the real proof is a TCP
connection with the RLPx handshake and an eth Status for network 11155111.
Specs: github.com/ethereum/devp2p (discv4.md, enr.md, dnsdisc.md).
"""
import argparse
import base64
import os
import socket
import struct
import subprocess
import time
import zlib

import rlp
from eth_hash.auto import keccak
from eth_keys import keys

# Sepolia bootnodes (EF NodeOps) as shipped in go-ethereum v1.17.7, params/bootnodes.go.
BOOTNODES = [
    "enr:-KG4QHn1Os72NN5Z44Ehf9CHPmN490GBphTFirfk33pFuWnkY9UNrQMfHkuCw6a-ykeRhGKrPL5ppMXiZQchgPrCDJQBgmlkgnY0gmlwhNRj2kKDaXA2kCoAHKALAA0CAAAAAAAAAF6Jc2VjcDI1NmsxoQJK_ye9jx9mela-ME_Kt5e017Ywv3hYH_5rwNhIUrtzNoN1ZHCCTriEdWRwNoJOuA",
    "enr:-KG4QCpiRltiyX7LgozCkQ8XBrcDzejBXbE7YRUQFmIEgv46SC8B4CJLcOO3O2bH-_u-B242GTzO_hQPhv3rpyvAHXMBgmlkgnY0gmlwhIHUpj2DaXA2kCYEqIAABAHQAAAAA2GdUACJc2VjcDI1NmsxoQNmVWXve5c0uvsn_agjTKQ8pR6gl9DynZyTQNruBDfJ4YN1ZHCCdsOEdWRwNoJ2ww",
    "enr:-KG4QJUWvc8qvCUr7x-qCFWee6HPLgmRUfVn-M_yqXD-sjSoAalKCV3NgUhmlqozjTJIj_nPr7J_uT8WJwbWiIx3Cv4BgmlkgnY0gmlwhJB-_BiDaXA2kCQAYYABAADQAAAAAYEgYAGJc2VjcDI1NmsxoQKOQetrA-97TELUzuGegVD1_cHKKNnjOmJ9CYdeSTor7YN1ZHCCdsOEdWRwNoJ2ww",
    "enr:-KG4QIBsIiST0JBBvgSNxvHQOeH5PkRgWeufahVXYQQWj7GLYPFDsviuQ6qU-3Xx4qnARaZiI-qqVRyw_2qqmOKRY1UBgmlkgnY0gmlwhLKc14yDaXA2kCoBBP8A9DxKAAAAAAAAAAGJc2VjcDI1NmsxoQKx4n3wy0KtwnuZCHmlxMmc4b0VvfC4Ka_f3sUPvjUq1oN1ZHCCdsOEdWRwNoJ2ww",
    "enr:-KG4QPM5C6wuTxu5yk4jkI9Mm64iSUKMPNt8y8kmoFzOlYrnYV4SBeZK7UiS4YWrkX4y2FD9_eH7xiPGvLQ-nXoXIcYBgmlkgnY0gmlwhAXfXlGDaXA2kCoBBP8C8BytAAAAAAAAAAGJc2VjcDI1NmsxoQIC3FMD8Si9DIBVofsSbJkpvXTUAfyi8q66XXQgjRhUsIN1ZHCCdsOEdWRwNoJ2ww",
]
DNS_TREE = "all.sepolia.ethdisco.net"
# EIP-2124 fork id: crc32 of the genesis, then of every fork block/time already
# passed (go-ethereum v1.17.7, params/config.go).
SEPOLIA_GENESIS = bytes.fromhex("25a5cc106eea7138acab33231d7160d69cb777ee0c2c553fcddf5138993e6dd9")
SEPOLIA_BLOCKS = [1735371]
SEPOLIA_TIMES = [1677557088, 1706655072, 1741159776, 1760427360, 1761017184, 1761607008, 1791294816]
PING, PONG, FINDNODE, NEIGHBORS, ENRREQUEST, ENRRESPONSE = 1, 2, 3, 4, 5, 6


def sepolia_fork_hash(now):
    crc = zlib.crc32(SEPOLIA_GENESIS)
    for fork in SEPOLIA_BLOCKS + [t for t in SEPOLIA_TIMES if t <= now]:
        crc = zlib.crc32(struct.pack(">Q", fork), crc)
    return struct.pack(">I", crc)


def enr_fields(record):
    """ENR (EIP-778) as a decoded RLP list: [signature, seq, key, value, ...]."""
    pairs = {record[i]: record[i + 1] for i in range(2, len(record) - 1, 2)}
    node = {"ip": None, "udp": None, "tcp": None, "id": None, "fork": None}
    if b"ip" in pairs and len(pairs[b"ip"]) == 4:
        node["ip"] = socket.inet_ntoa(pairs[b"ip"])
    for port in ("udp", "tcp"):
        if port.encode() in pairs:
            node[port] = int.from_bytes(pairs[port.encode()], "big")
    if b"secp256k1" in pairs:
        node["id"] = keys.PublicKey.from_compressed_bytes(pairs[b"secp256k1"]).to_bytes()
    if b"eth" in pairs:  # [[fork hash, fork next]]
        eth = pairs[b"eth"]
        eth = rlp.decode(eth) if isinstance(eth, bytes) else eth
        node["fork"] = eth[0][0]
    return node


def enr_text(text):
    body = text.removeprefix("enr:")
    return enr_fields(rlp.decode(base64.urlsafe_b64decode(body + "=" * (-len(body) % 4))))


def verdict(node, wanted):
    if node["fork"] is None:
        return "-", "sin 'eth' (consenso u otra red)"
    return node["fork"].hex(), "SEPOLIA" if node["fork"] == wanted else "otra red / otra versión"


def table(nodes, wanted):
    print(f"  {'IP':<16} {'TCP':>5}  {'fork hash':<9} red")
    for node in nodes:
        fork, net = verdict(node, wanted)
        print(f"  {node['ip'] or '?':<16} {node['tcp'] or '-':>5}  {fork:<9} {net}")
    print(f"\n  {sum(n['fork'] == wanted for n in nodes)} de {len(nodes)} anuncian Sepolia.")


# --- DNS (EIP-1459) --------------------------------------------------------

def txt(name):
    try:
        out = subprocess.run(["dig", "+short", "+time=3", "+tries=2", "TXT", name],
                             capture_output=True, text=True, timeout=15).stdout
    except subprocess.TimeoutExpired:
        return ""
    return "".join(part for part in out.replace("\n", "").split('"') if part.strip())


def dns(limit, wanted):
    print(f"Paso 1. Raíz del árbol:  dig +short TXT {DNS_TREE}")
    root = txt(DNS_TREE)
    print(f"        {root[:90]}…")
    fields = dict(item.split("=", 1) for item in root.split()[1:])
    print(f"Paso 2. Recorrer ramas desde e={fields['e']} (cada rama lista hashes de hijos)")
    pending, nodes = [fields["e"]], []
    while pending and len(nodes) < limit:
        name = pending.pop()  # depth first: reach leaves after a few lookups
        value = txt(f"{name}.{DNS_TREE}")
        if value.startswith("enrtree-branch:"):
            pending.extend(reversed([h for h in value.removeprefix("enrtree-branch:").split(",") if h]))
        elif value.startswith("enr:"):
            nodes.append(enr_text(value))
            print(f"        hoja {name}: enr:… ({len(nodes)}/{limit})")
    print("Paso 3. Fichas (ENR) decodificadas\n")
    table(nodes, wanted)
    return nodes


# --- discv4 (UDP) ----------------------------------------------------------

class Discovery:
    """One UDP socket, one throwaway identity, the packets we need."""

    def __init__(self):
        self.key = keys.PrivateKey(os.urandom(32))
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind(("0.0.0.0", 0))

    def send(self, kind, payload, address):
        # packet = keccak(signature || type || data) || signature || type || data
        data = bytes([kind]) + rlp.encode(payload)
        signature = self.key.sign_msg_hash(keccak(data)).to_bytes()
        packet_hash = keccak(signature + data)
        self.sock.sendto(packet_hash + signature + data, address)
        return packet_hash

    def receive(self, timeout):
        """Yields (kind, payload, sender id) until timeout; drops bad packets."""
        end = time.time() + timeout
        while (left := end - time.time()) > 0:
            self.sock.settimeout(left)
            try:
                packet, address = self.sock.recvfrom(1280)
            except socket.timeout:
                return
            if len(packet) < 98 or keccak(packet[32:]) != packet[:32]:
                continue
            signature = keys.Signature(packet[32:97])
            sender = signature.recover_public_key_from_msg_hash(keccak(packet[97:])).to_bytes()
            payload = rlp.decode(packet[98:], strict=False)
            if packet[97] == PING:  # answering is how they learn we are a real node
                self.send(PONG, [payload[1], packet[:32], int(time.time()) + 20], address)
            yield packet[97], payload, sender

    def ping(self, ip, port):
        here, there = [b"\0\0\0\0", 0, 0], [socket.inet_aton(ip), port, port]
        return self.send(PING, [4, here, there, int(time.time()) + 20], (ip, port))


def discv4(bootnode, limit, wanted):
    d = Discovery()
    boot = enr_text(bootnode)
    address = (boot["ip"], boot["udp"])
    print(f"Paso 1. Identidad temporal {d.key.public_key.to_hex()[:18]}… en UDP {d.sock.getsockname()[1]}")
    print(f"Paso 2. Bootnode {boot['ip']}:{boot['udp']} (de la lista de Geth)")
    print("Paso 3. Ping → esperar su Pong y su Ping (le contestamos: así nos «conoce»)")
    start, sent, ponged, pinged = time.time(), d.ping(*address), False, False
    for kind, payload, sender in d.receive(5):
        if sender != boot["id"]:
            continue
        if kind == PONG and payload[1] == sent:
            ponged = True
            print(f"        Pong en {1000 * (time.time() - start):.0f} ms, firmado con su clave")
        pinged |= kind == PING
        if ponged and pinged:
            break
    if not ponged:
        raise SystemExit("        Sin respuesta: prueba otro --bootnode o usa --dns.")
    time.sleep(0.5)

    print("Paso 4. FindNode: «¿a quién conoces?» → Neighbors")
    d.send(FINDNODE, [os.urandom(64), int(time.time()) + 20], address)
    found = {}
    for kind, payload, sender in d.receive(3):
        if kind == NEIGHBORS and sender == boot["id"]:
            for ip, udp, tcp, node_id in payload[0]:
                if len(ip) == 4:
                    found[node_id] = (socket.inet_ntoa(ip), int.from_bytes(udp, "big"), int.from_bytes(tcp, "big"))
    print(f"        {len(found)} nodos")
    if not found:
        raise SystemExit("        Ninguno: prueba otro --bootnode.")

    chosen = list(found.items())[:limit]
    print(f"Paso 5. A {len(chosen)} de ellos: Ping, esperar a conocernos, y ENRRequest (su ficha)")
    nodes = {node_id: {"ip": ip, "udp": udp, "tcp": tcp or None, "id": node_id, "fork": None}
             for node_id, (ip, udp, tcp) in chosen}
    bonded, asked, answered = set(), {}, set()
    for node_id, node in nodes.items():
        d.ping(node["ip"], node["udp"])
    for attempt in range(3):  # they answer ENRRequest only once they have pinged us back
        for kind, payload, sender in d.receive(1.5):
            if sender not in nodes:
                continue
            if kind in (PONG, PING):
                bonded.add(sender)
            elif kind == ENRRESPONSE and asked.get(sender) == payload[0]:
                nodes[sender].update({k: v for k, v in enr_fields(payload[1]).items() if v is not None})
                answered.add(sender)
        for node_id in bonded - answered:
            node = nodes[node_id]
            asked[node_id] = d.send(ENRREQUEST, [int(time.time()) + 20], (node["ip"], node["udp"]))
    print(f"        {len(bonded)} contestaron al Ping, {len(answered)} mandaron su ficha")
    nodes = list(nodes.values())
    print("Paso 6. Resultado\n")
    table(nodes, wanted)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--dns", action="store_true", help="read the DNS tree instead of asking over UDP")
    parser.add_argument("--bootnode", type=int, default=0, help=f"0..{len(BOOTNODES) - 1}")
    parser.add_argument("--nodes", type=int, default=10)
    parser.add_argument("--enodes", help="with --dns: write the Sepolia nodes as enode:// lines to this file")
    args = parser.parse_args()
    wanted = sepolia_fork_hash(int(time.time()))
    print(f"Sepolia hoy tiene fork hash {wanted.hex()} (EIP-2124).\n")
    if args.dns:
        nodes = dns(args.nodes, wanted)
        if args.enodes:
            with open(args.enodes, "w") as out:
                for n in nodes:
                    if n["fork"] == wanted and n["id"] and n["ip"] and n["tcp"]:
                        out.write(f"enode://{n['id'].hex()}@{n['ip']}:{n['tcp']}\n")
    else:
        discv4(BOOTNODES[args.bootnode], args.nodes, wanted)
    print("\nSolo son candidatos: la prueba final es TCP + saludo RLPx + Status de Sepolia.")


if __name__ == "__main__":
    main()
