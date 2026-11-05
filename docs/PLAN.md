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
