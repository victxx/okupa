# Okupa — plan

Repositorio privado. Práctica con formato de hackathon "from scratch": se empieza
de cero, commits pequeños y continuos, y cada pieza se da por buena solo con
evidencia reproducible.

## La idea en una frase

Un okupa se ha metido a vivir en tu DSi: tiene casa, días propios y un diario; te
pide cosas, tú se las mandas desde la consola directamente a Ethereum, y él las
cuelga, las recuerda y regala cosas a otros okupas.

## Premios objetivo (simulación)

| Premio | Qué demuestra Okupa |
|---|---|
| EF — Best CROPS App | Envío por devp2p sin RPC ni relé (C), cliente mínimo abierto (O), nadie ve la cuenta al enviar (P), clave que nunca sale de la consola (S) |
| 0G — Physical AI (+ mejor build en hardware real) | La DSi es el cuerpo; 0G decide en el bucle; un request id y un episodio en 0G Storage por decisión; parada segura = dormir |

Chainlink queda fuera, salvo que se confirme que un dispositivo puede verificar
por sí mismo un informe firmado por una DON de CRE (sustituiría al lector de
confianza).

## Por qué Ethereum es el corazón

Ethereum no se usa como disco: lo pesado (diario, mallas) vive en 0G. En cadena
solo van **actos firmados y pequeños**: un cuadro, un regalo, un saludo.

- **Entrada sin permiso.** La capa 1 es la red grande donde cualquiera entrega una
  transacción a los validadores por P2P público. Una L2 tiene secuenciador; un juego
  normal, un servidor. Okupa entra desde una consola de 2009 sin pedir permiso a nadie.
- **Lo tuyo sobrevive al juego.** Cada cuadro y cada regalo lo firma una clave que
  nunca sale de la consola, con autor, destinatario y fecha públicos. Si el juego
  desaparece, la pared y los regalos siguen ahí y cualquiera puede reconstruirlos.
- **Un sello, no una comisión.** Enviar algo cuesta como un sello de correos, y eso
  le da valor: se mandan pocas cosas y con intención.

Coste en mainnet (2026-11-05: base 0,74 gwei, mediana 3 h 1,9 gwei, pico 14,3 gwei;
propina mediana 0,46 gwei; ETH ≈ 2.000 €). Gas con el suelo de calldata de EIP-7623
y todos los bytes distintos de cero (caso peor):

| Acto | Gas | Hoy | Mediana | Pico |
|---|---|---|---|---|
| Saludo (8 B) | ~21.300 | 0,05 € | 0,10 € | 0,63 € |
| Regalo objeto (72 B) | ~23.900 | 0,06 € | 0,11 € | 0,71 € |
| Dibujo (520 B) | ~41.800 | 0,10 € | 0,20 € | 1,24 € |
| Foto (1032 B) | ~62.300 | 0,15 € | 0,30 € | 1,84 € |

La demo usa Sepolia (gratis). Si un cambio futuro encarece el calldata, el coste
sube; conviene revisarlo antes de cualquier entrega.

## Reglas del proyecto

- El camino de envío es siempre la consola: firma local → RLPx/eth → peers. Nunca
  RPC, proxy ni relé para enviar.
- El lector es de solo lectura y sustituible. Lo que diga es una observación de
  confianza, no consenso ni finalidad verificados en la DSi; decirlo así siempre.
- 0G decide solo lo que el okupa **hace y dice** (de conjuntos cerrados) y los
  objetos que encuentra. Nunca la inclusión, la pared, la pendiente ni el nonce.
- Sin 0G: el okupa duerme, con una frase fija que no se atribuye a 0G.
- Una sola transacción pendiente; reintentar envía exactamente los mismos bytes.
- Guardar antes de mostrar: borradores, pendientes, observaciones y decisiones.
- Claves, entropía y datos de ejecución en `local/` y `run/`.
- Textos del juego en inglés. Estética mínima: fondo carbón, sans serif, cuatro
  tonos, sin arte generado salvo los objetos regalo.

## Qué es el MVP

1. La DSi firma y envía una foto o un dibujo a Sepolia por P2P.
2. Se incluye; el lector lo confirma (HMAC) y el cuadro se cuelga.
3. 0G decide la reacción del okupa (gesto + frase) y la registra en su diario.
4. La DSi muestra la reacción; reabrir conserva pared, diario y pendiente.
5. Regalo: un cuadro enviado a la cuenta de otro okupa aparece en su casa.

Después, por orden: el día del okupa y sus peticiones; objetos 3D; visitas.

## Formato en cadena (borrador)

Toda la actividad son transacciones tipo 2 de valor cero, firmadas en la DSi.

| Tipo | Destino | Calldata |
|---|---|---|
| hello | la propia cuenta | `OKPA 01 00` |
| dibujo | la propia cuenta | `OKPA 01 01` + 64×64 a 1 bpp (512 B) |
| foto | la propia cuenta | `OKPA 01 02` + 64×64 a 2 bpp (1024 B) |
| regalo cuadro | la cuenta del otro okupa | `OKPA 01 03` + dibujo o foto |
| regalo objeto | la cuenta del otro okupa | `OKPA 01 04` + root de 0G Storage (32 B) + keccak de la malla (32 B) |

La DSi comprueba la malla de un objeto por su keccak; el root solo sirve para
encontrarla.

## Objetos 3D

- Cosas pequeñas: manzana, banana, peluche… Máximo 200 triángulos, cuatro tonos,
  formato binario propio de pocos KB.
- Primero un prototipo: ¿escribe el modelo de 0G una malla aceptable dentro de
  esos límites? Si no, 0G elige y combina piezas de un catálogo pequeño hecho a mano.
- El lector valida (triángulos, tonos, tamaño) antes de guardar en 0G Storage.

## Arquitectura

```text
DSi / host ──firma── RLPx/eth ──► peers de Sepolia
     ▲                               │
     │ informe HMAC                  ▼
  lector (solo lectura) ◄─── RPC de lectura sustituible
     │
     └── mente en 0G: Compute (decisión, TEE) + Storage (diario, mallas)
```

- `core/`: C99 sin heap ni E/S. Keccak, RLP, secp256k1 (librería revisada,
  versión fijada), transacciones tipo 2, ECIES, RLPx, eth (Status, Transactions).
- `platform/host` y `platform/dsi`: sockets, reloj, almacenamiento, entropía.
- `game/`: estado de la casa, UI compartida host/DSi, cámara, regalos.
- `reader/`: Python; observador, puente LAN autenticado, mente 0G.

## Hitos (cada uno = varios commits pequeños)

| # | Hito | Evidencia de "hecho" |
|---|---|---|
| 0 | Esqueleto, build host, CI | `make test` verde en CI |
| 1 | Keccak, RLP, claves, firma de tx tipo 2 | vectores oficiales + tx aceptada por Geth local |
| 2 | ECIES + handshake RLPx + Hello/Status | sesión con Geth local registrada en ambos lados |
| 3 | Envío P2P desde el host a Sepolia | tx en un explorador, sin RPC en el envío |
| 4 | Port a DSi: arranque, SD, Wi-Fi, mismo núcleo | foto de la DSi con la tx en Sepolia |
| 5 | Casa, okupa, cuaderno, cámara | renders host + fotos de la DSi |
| 6 | Lector + informe HMAC + pared | cuadro colgado y reapertura en la DSi |
| 7 | Mente 0G: reacción, diario, parada segura | request id + root de Storage + reacción en la DSi |
| 8 | Regalos entre okupas | regalo visto en la casa de otra cuenta |
| 9 | Día del okupa y peticiones | decisiones con request id en el diario |
| 10 | Objetos 3D | malla validada, guardada y vista en la DSi |
| 11 | README CROPS, vídeo ≤ 3 min | entrega simulada completa |

## Decisiones pendientes

- Autorizar que 0G elija la actividad del día y las peticiones (además del gesto).
- Proveedor de 0G Compute en testnet: ahora mismo no hay ninguno para el modelo de visión.
- Licencia (EF pide una libre) y visibilidad: el repo sigue privado.
