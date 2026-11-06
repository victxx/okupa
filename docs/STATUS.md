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
| Descubrimiento solo con el núcleo | `build/okupa-dnswalk` recorre el árbol DNS de Sepolia verificando raíz firmada, hashes de cada hijo y firma de cada ENR: 40 nodos, 47 consultas, 0 registros rechazados. Encadenado con `okupa-peer`: 34 de 40 completan el saludo RLPx; todos los que contestan dicen "too many peers" en esa pasada |
| ROM de la DSi | Habitación, botones, cuaderno, guardado en SD (el okupa vive el tiempo que estuvo apagada) y pantalla Dev con el núcleo cronometrado. Compilada; pendiente de probar en la consola |

| 0G en el núcleo | Verificar una respuesta TEE (texto sha256(petición):sha256(respuesta) rehecho con nuestros bytes + firma EIP-191), token de 0G Direct firmado por la propia clave (idéntico al del SDK oficial 0.9.0 con ethers), lector JSON, el "cerebro" (pregunta y lectura de conjunto cerrado) y HTTP/1.1 |
| 0G testnet (laboratorio en el Mac) | Cuenta de laboratorio con 5,05 0G; ledger abierto (3 0G), firmante TEE de `glm-5.3` reconocido y 1 0G en su subcuenta. Ninguna inferencia todavía: los ocho servicios de testnet cortan el TLS y el SDK marca `glm-5.3` como "critical" |

| TLS en el núcleo | BearSSL con 8 raíces fijadas; `okupa-https` lee el saldo 0G por HTTPS solo con el núcleo. BearSSL llega a TLS 1.2: un servidor solo TLS 1.3 (el indexador de 0G Storage) lo rechaza |
| Sesión y descubrimiento como núcleo | La sesión devp2p y el recorrido del árbol DNS viven en `core/` con callbacks de E/S; el Mac (`okupa-peer`, `okupa-dnswalk`) y la ROM usan el mismo código; loopback y Geth vuelven a pasar |
| Núcleo en la DSi real (foto IMG_5985, ROM 5973ac6) | Todas las comprobaciones pasan en la consola a 133 MHz: clave pública 23 ms, firmar una tx 30 ms, ECDH 43 ms, escribir un auth RLPx 161 ms, leerlo 140 ms. Guardado en SD funciona |
| Red en la DSi | Dev → Network: Wi-Fi guardado en la consola, lista de Sepolia por DNS verificada y sesiones hasta que un nodo tenga hueco. La ROM 5973ac6 daba data abort al pulsar Network (pedía la MAC antes de iniciar el Wi-Fi); corregido. Dev → Wi-Fi busca redes, pide la contraseña con teclado en pantalla y la recuerda en `sd:/okupa/wifi.txt` (texto plano en la SD). Claves de saludo con entropía débil (temporizadores, reloj, MAC): solo para claves desechables |
| Red en la DSi, prueba física (IMG_5991) | Primera pasada: Wi-Fi por hotspot, lista de Sepolia verificada en la consola y saludo RLPx con los nodos; todos "full" o "closed". Segunda pasada: dswifi devolvió 255.255.255.255 como DNS y la consola se quedó esperando. Corregido: esperas acotadas fotograma a fotograma y DNS de respaldo (pasarela, luego 1.1.1.1) |
| discv4 en el núcleo | Paquetes contra los vectores de EIP-8 e idénticos a eth-keys; rastreador acotado (384 nodos) que identifica por dirección y solo verifica firmas de ENR. Desde el Mac en 90 s: 384 nodos, 348 fichas verificadas, 7 de Sepolia. Con el rastreo en Python (3000 nodos): 9 de Sepolia y 1 Reth admitió la sesión completa |
| Red en la DSi: orden de búsqueda | Nodos que ya nos admitieron (`sd:/okupa/peers.txt`), luego la lista DNS y, si nadie tiene hueco, 45 s de discv4. Compilada, sin probar en la consola |
| Red en segundo plano en la DSi | Un hilo cooperativo ("cartero") arranca con la consola: se une a la red Wi-Fi recordada más fuerte (hasta 6, `sd:/okupa/wifi.txt`) o a los ajustes de la consola, prueba nodos conocidos, la lista DNS y discv4, reintenta cada 20 s y, al entrar, se queda conectado respondiendo pings. El juego no espera; la pantalla de inicio muestra el estado. Primera prueba física (fd83257): DNS bien, 24 nodos de la lista (21 llenos, 3 cerrados); discv4 no recibió nada (5 vistos), pendiente de ver con los contadores nuevos |
| Mainnet, solo saludos (sin transacciones) | Fork id de mainnet en el núcleo (07c9462e, de la configuración de Geth 1.17.7). Lista DNS: 80 nodos, 76 saludos RLPx, 0 admitidos (75 "too many peers"). discv4 en 90 s: 90 nodos de mainnet; de 71 fuera de la lista DNS, 2 admitieron la sesión completa (Nethermind 2.2.1 y Reth 2.7.0). Mismo ritmo por intento que Sepolia, pero muchos más candidatos por minuto |
| Coste en mainnet (2026-11-06, base 0,072 gwei + propina 0,014, ETH = 2007 €) | Saludo 21 210 gas ≈ 0,004 €; dibujo 41 720 gas ≈ 0,007 €; 1 KB ≈ 62 000 gas ≈ 0,011 € (suelo de calldata EIP-7623 incluido) |
| DSi en mainnet | El cartero usa mainnet por defecto (`network.txt` con "sepolia" para volver): nodos conocidos (`peers-mainnet.txt`), lista DNS como semilla, 30 s de discv4 desde los bootnodes de Geth 1.17.7 y esos nodos, y la lista DNS al final. Sin transacciones. Sin probar en la consola |
| Cuenta de la consola | Dev → Wallet: la clave se crea en la DSi a partir de ~480 muestras del stylus con su instante exacto (temporizador de 33 MHz) y el reloj, con Keccak; se guarda en `sd:/okupa/account.key` (texto plano en la SD) y se muestra la dirección EIP-55. No firma ni envía nada todavía |
| 0G proveedor qwen | Volvió el TLS; nuestro token pasa el proxy 0G, pero el servicio de Alibaba detrás responde 401 "API-key is blocked" (la clave del proveedor) |

No se ha enviado ninguna transacción a Ethereum. En 0G testnet, solo las cuatro de preparación del ledger con la cuenta de laboratorio.

## Siguiente

0. En cuanto un proveedor de 0G testnet responda: una decisión real del okupa verificada en C.

1. Probar la ROM en la DSi y anotar los tiempos de la pantalla Dev.
2. Que Geth local acepte una transacción nuestra en su txpool (cadena de pruebas, sin dinero).
3. Llevar el descubrimiento a la DSi (UDP con el DNS que da el router) y reintentar peers hasta que uno admita la sesión.
