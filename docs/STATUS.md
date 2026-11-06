# Estado

Qué está hecho y con qué evidencia. Los registros completos quedan en `run/` (fuera de Git).

## 2026-11-06

| Pieza | Evidencia |
|---|---|
| Núcleo: Keccak, RLP, claves, tx EIP-1559 firmada | Vectores oficiales; bytes idénticos a eth-account 0.13.7 |
| ECIES, auth/ack RLPx, secretos, frames | Vectores EIP-8 y un escritor independiente en Python |
| Snappy, Hello, Ping, Disconnect, Status eth/69, fork id de Sepolia | Tests contra cramjam, pyrlp y los vectores EIP-2124 |
| Sesión con Geth 1.17.7 local | Geth registra `Ethereum peer connected name=okupa/0.1`; `tools/geth_session.sh` |
| Sesión con un nodo público de Sepolia | 25 nodos por DNS (EIP-1459): 22 completan el saludo RLPx, 8 envían Hello (Erigon, Reth), la mayoría responde "too many peers" y un Reth 2.7.0 acepta la sesión completa: Status de Sepolia (bloques 12000000..12075634), Ping/Pong y despedida limpia |
| ROM de la DSi | Habitación, botones, cuaderno, guardado en SD y pantalla Dev con el núcleo cronometrado. Compilada; pendiente de probar en la consola |

No se ha enviado ninguna transacción a ninguna red.

## Siguiente

1. Probar la ROM en la DSi y anotar los tiempos de la pantalla Dev.
2. Que Geth local acepte una transacción nuestra en su txpool (cadena de pruebas, sin dinero).
3. Descubrimiento DNS y ENR en C, para que la consola encuentre nodos sola.
