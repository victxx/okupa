# Entrega — ETHGlobal Mumbai (EF + 0G)

Revisado el 2026-11-06 a las 21:45 IST contra `main` d7c95c1 y las ramas `mind`
(262df14) y `visual` (0ce8e27). Fuentes al final; las citas son literales.

## En 30 segundos

- **Plazo: sábado 7 nov, 09:00 IST.** Quedan unas 11 h, sueño incluido.
- **Premios:** 0G *Physical AI* (carril "personal devices" + premio a hardware real)
  y EF *Best CROPS App*. Nada más: ningún otro sponsor encaja sin traicionar el diseño.
- **Lo que hoy no aprueba ningún juez:** ninguna transacción enviada a Ethereum,
  ninguna inferencia real de 0G, repo privado y sin licencia, y un README que
  promete cosas que no existen todavía.
- **Las cuatro cosas que lo cambian todo (≈5 h):** juntar `mind` + `visual` en
  `main` y probar la ROM; un hello firmado en la DSi e incluido; una decisión real
  de 0G verificada en la consola; README honesto con CROPS, 0G y uso de IA.

## Reglas que afectan (Mumbai)

| Regla | Cita | Okupa |
|---|---|---|
| Plazo | "All projects must be submitted by Saturday, November 7th 2026 at 09:00 am IST" | ⏳ |
| Vídeo | "2-4 minute demo video"; "Videos under 2 minutes or over 4 minutes will be automatically rejected" | EF y 0G piden ≤ 3 min → **2:45–2:55** |
| Vídeo, prohibido | "DO NOT use mobile phones to record", "DO NOT use a text to speech synthesizer / AI Voiceover", "DO NOT speed up the video", "< 720p" | Grabar la DSi con cámara/webcam, voz propia |
| From scratch | "Any prior project-specific code, designs, or assets are not allowed unless they're from public libraries or starter kits." / "won't qualify for partner prizes or the Finalist category" | ⚠️ ver Riesgos |
| Historial | "Submissions with large single commits or missing histories may be disqualified" | ✅ 78 commits pequeños en `main` (109 con ramas) desde el 5 nov 23:20 IST |
| IA | "Clearly document in your submission where and how AI tools were used … which parts of the code, specific files, or assets" | ❌ no hay sección |
| IA, flujos con specs | "you must include all spec files, prompts, and planning artifacts in your submission repository" | ⚠️ `docs/PLAN.md` sí; `AGENTS.md` no está en Git |
| IA, peso | "Submissions that rely entirely on AI without meaningful contributions from team members may not be eligible" | ⚠️ contar lo que hizo el equipo |
| Partners | "up to 3 Partner Prizes … multiple tracks … only counting as 1 Partner Prize" | 0G (todas sus pistas = 1) + EF |
| EF | "One EF prize per project" | Solo CROPS |
| Finalistas | "7 minutes … 4 minutes for the demo, followed by 3 minutes for Q&A"; criterios Technicality, Originality, Practicality, Usability, WOW Factor | Requiere estar en la sala |

## 0G — Physical AI: Agents with a Body ($5.000; 3.º = "Best build on real hardware")

Encaje: el carril "Personal devices" pide literalmente "a pocket device or a
desktop companion, with the user's memory on 0G Storage and sealed inference".

| Requisito (literal) | Estado | Evidencia / hueco |
|---|---|---|
| "A 0G call inside the decision loop" | ⚠️ parcial | Código completo en `mind` (`game/src/think.c`, `platform/dsi/mind.c`, Y en Dev); **ninguna inferencia real**: testnet sin proveedor útil (qwen 401), mainnet "account not exists" |
| "An episode log on 0G Storage with a request id per decision" | ⚠️ parcial | Un episodio subido y releído (root `0x9b57…af27`), pero es la **parada segura**, sin request id de una decisión real. Lo sube el Mac (indexador solo TLS 1.3) |
| "A replayable simulation, or a live hardware demo at the 0G booth" | ⚠️ | DSi real = fuerte; la repetición de episodios en host no existe. Booth exige presencia |
| "A safe-stop fallback … A fallback that fakes the decision disqualifies." | ✅ | Siesta + frase fija no atribuida a 0G; tests con firmante falso, bytes cambiados, 401, red caída |
| Not wanted: "a chatbot with a robot avatar … 0G used only for logs" | ✅ | Conjunto cerrado de gestos/acciones, no conversa |
| Public GitHub repo (README + setup) | ❌ | Privado, README "Just started" |
| "Demo video (under 3 mins) and a live demo link" | ❌ | Ni vídeo ni enlace. Enlace posible: release con la ROM + instrucciones host |
| "Contract deployment addresses" (sin "if any") | ❌ | Okupa no despliega contratos. Preguntar a 0G; si insisten, ver mejora 7 |
| "user's memory on 0G Storage" (carril) | ❌ | `think.c` no relee el diario: hoy es un registro, no memoria. No llamarlo memoria hasta que lo sea |

Puntos fuertes que nadie más tendrá: firma del TEE comprobada **en la consola en C**
antes de leer la respuesta; token Direct firmado por la clave de la propia DSi;
hardware de 2009 real.

## EF — Best CROPS App ($3.500)

Lead property: **C**. Encaja con las ideas del propio EF: "a wallet a user can fully
recover with nothing from you: no server, API or relayer".

| Requisito (literal) | Estado | Hueco |
|---|---|---|
| "Public repo under a free and open-source license … README that explains how to run it" | ❌ | Privado, sin `LICENSE` (todas las dependencias son MIT/dominio público → MIT encaja) |
| README section "CROPS", una o dos líneas por letra | ❌ | Borrador abajo |
| Vídeo ≤ 3 min "showing the lead property end to end" (p. ej. "show server logs") | ❌ | Mostrar que no hay RPC: captura de tráfico en el hotspot |
| "works end to end in the demo" | ❌ | **Cero transacciones enviadas.** El hello firmado en la DSi (d7c95c1) está compilado, sin probar |
| "No third-party trackers or analytics"; claves en el dispositivo o explicado | ✅/⚠️ | Sin trackers. `account.key` y `wifi.txt` en texto plano en la SD: decirlo |
| "structural (works without you) beats cosmetic" | ✅ | No hay "you": no hay servidor que apagar |

Borrador de la sección CROPS (ajustar a lo que sea verdad al entregar):

- **C** — La DSi encuentra nodos con la lista DNS firmada (EIP-1459) y discv4 y
  entrega su transacción por RLPx/eth/69. No hay RPC, relé ni servidor nuestro que
  apagar. Si 0G cae, el okupa duerme; enviar sigue funcionando.
- **O** — MIT; se compila desde el repo (`make`, BlocksDS para la ROM); dependencias
  fijadas por commit/hash en `THIRD_PARTY.md`.
- **P** — La DSi tiene la privacidad de un nodo propio, no la de una wallet en un
  proveedor: no hay cuenta, API key ni proveedor que junte todas tus lecturas. Como
  con Geth en casa, el primer peer ve la IP junto a la transacción (pública de todos
  modos); el resolvedor DNS ve la consulta del árbol de nodos; el proveedor de 0G ve
  el estado del okupa que se le pregunta (sin datos personales).
- **S** — La clave nace en la DSi (≈480 muestras del stylus con un temporizador de
  33 MHz, Keccak) y nunca sale. Compromiso declarado: se guarda en texto plano en la
  SD; las claves efímeras del saludo usan entropía débil (solo protegen el canal).
  **Solo si la mejora 9 funciona en la DSi:** "No te pueden mentir: la consola
  comprueba su inclusión con pruebas de Merkle (`transactionsRoot`, `receiptsRoot`)
  sobre cabeceras en las que coinciden varios peers independientes; no verifica el
  consenso (BLS, siguiente paso)". Si no funciona, esa frase va a "Next".

### Anon-RPC: por qué no

Es un sistema de workers JS/Wasm ejecutados por un harness de navegador, con
transporte KPS (QUIC/TLS 1.3 o WebRTC) y workers de hasta ~18 MB; "Browser-only"
según su propia guía. En la DSi acabaría siendo una pasarela = un relé, que rompe la
C. Además su criterio mide "how much of the app's traffic goes through Anon-RPC" (en
la DSi, 0 %) y solo se gana un premio EF. De su propuesta tomamos la otra mitad:
verificar pruebas, hecho por P2P sin RPC (mejora 9).

## Riesgos que un juez va a sacar

1. **Trabajo previo.** La regla excluye de partners y finalistas cualquier código,
   diseño o asset específico del proyecto anterior al kick-off, salvo librerías
   públicas o starter kits. El historial empieza el 5 nov a las 23:20 IST (confirmar
   la hora oficial de inicio). Si algo del repo existía antes, es el riesgo n.º 1 de
   elegibilidad; los jueces miran el historial "during or after the event".
2. **Uso de IA.** Hay que declararlo por partes/ficheros. Contar qué hizo el equipo:
   diseño, decisiones, pruebas en la consola, fotos, depuración en hardware.
3. **README que promete de más.** Hoy dice "checks its own inclusion", "the camera is
   its eyes", "Gifts between companions", "No server of ours anywhere" y "diary lives
   on 0G Storage". Nada de eso existe tal cual (el diario lo sube el Mac). Un juez que
   lo note descuenta todo lo demás. Pasarlo a "Next".
4. **Inclusión.** La DSi no comprueba que su tx entró; el vídeo usará Etherscan
   (decir: "solo para que tú lo veas; la consola no lo usa").
5. **Ramas sin juntar.** `mind` y `visual` no están en `main`; la ROM de la demo debe
   tener las tres cosas y estar probada en la DSi.
6. **Red en la sala.** Wi-Fi del recinto saturado → llevar hotspot y nodos conocidos
   (`peers-mainnet.txt`) ya guardados.
7. **Repo privado.** Ambos premios piden repo público. Hacerlo público lo decide y lo
   hace el usuario; nadie más.

## Qué hacer, por impacto por hora

| # | Tarea | Horas | Desbloquea |
|---|---|---|---|
| 1 | Juntar `mind` y `visual` en `main`, `make test`, ROM, una pasada en la DSi | 1 | Una sola demo |
| 2 | **Un hello firmado en la DSi, incluido** (mainnet: ≈0,004 € de gas; fondear la dirección de la consola). Guardar hash y foto | 1–1,5 | EF entero, WOW |
| 3 | **Una decisión real de 0G** verificada en la DSi: fondear ~4,2 0G en mainnet y correr el procedimiento de `mind`; plan B la clave del router (decirlo). Subir ese episodio con su request id | 1–1,5 | 0G entero |
| 4 | README nuevo: qué es, cómo compilar/ejecutar, CROPS, "What breaks without 0G", "AI usage", límites honestos, "Next". `LICENSE` MIT | 0,75 | Ambos requisitos formales |
| 5 | Vídeo (guion abajo), voz propia, cámara que no sea móvil | 1,5 | Entrega |
| 6 | Formulario: descripción, pitch por pista, feedback a cada partner | 0,5 | Entrega |
| 7 | Contrato mínimo en 0G (índice episodio → root + request id) solo si 0G confirma que la dirección es obligatoria | 0,75 | Requisito formal 0G |
| 8 | Memoria real: meter las 3 últimas decisiones en la siguiente petición | 1 | Carril "personal devices" |
| 9 | Solo si 1–5 están hechos antes de las ~03:00 IST, en rama aparte: inclusión verificada por P2P (eth/69 GetBlockHeaders/Bodies/Receipts a varios peers, trie ordenado, `transactionsRoot` + `receiptsRoot`, estado 1). Primero en el Mac contra la tx del paso 2, luego en la DSi. Si no pasa en la consola, no se junta | 4–6 | S de CROPS, "can't be lied to" |

Recortar sin pena: regalos, amigos/QR, fotos y
cámara, objetos 3D, cliente ligero, visitas, Sepolia (elegir **una** red: mainnet).
Aparecen en "Next", nunca como hechos.

Decisiones del usuario antes de empezar: red y fondos del hello (2), gasto real en
0G mainnet (3), visibilidad del repo (7).

## Pitch (2 líneas por pista)

**EF — CROPS.** Okupa is a pet living on a 2009 Nintendo DSi that posts to Ethereum
with no RPC, relayer or server: the console finds peers itself (EIP-1459, discv4),
speaks RLPx/eth/69 and hands over a transaction it signed with a key born from your
scribbles. There is nothing of ours to switch off.

**0G — Physical AI.** A pocket companion whose body is a real DSi and whose every
decision comes from 0G sealed inference, checked on the console in C (TEE signature
over sha256(request):sha256(response)) before it moves. Each decision is logged to
0G Storage with its request id; if 0G is unreachable it naps instead of faking one.

**Qué se rompe sin 0G** (lo piden): el okupa no hace nada por su cuenta; sin una
decisión firmada por el TEE se queda dormido.

## Vídeo de 2:50

| Tiempo | Imagen | Voz (propia) |
|---|---|---|
| 0:00–0:12 | DSi en la mano, okupa en el desván | "Someone moved into my DSi. It's also a full Ethereum peer." |
| 0:12–0:30 | Tocar al okupa, su día, la hora | Qué es el juego, en una frase |
| 0:30–0:50 | Wallet: garabatos → dirección + QR | "Its key is born from my scribbles and never leaves the console." |
| 0:50–1:25 | Send hello → cartero: DNS/discv4, "N nodes take letters" | "No RPC. It finds nodes and hands them the transaction itself." |
| 1:25–1:40 | Etherscan con `from` = dirección de la DSi + captura del hotspot: solo puertos P2P | "Etherscan is only for you. The console never asked anyone." |
| 1:40–2:15 | Y → 0G: request id, "TEE signature OK", el okupa hace el gesto y dice la frase | "0G decides; the DSi checks the enclave's signature before it obeys." |
| 2:15–2:25 | Episodio en 0G Storage (root, request id) | "Every decision is logged on 0G Storage." |
| 2:25–2:38 | Cortar Wi-Fi a 0G → siesta | "No 0G, no decision. It naps — it never pretends." |
| 2:38–2:50 | Tarjeta: 30 ms firmar, < 0,5 s cripto, 0 servidores; CROPS | "Okupa. Just a DSi and a Wi-Fi." |

Para la final (4 min en vivo): misma secuencia con la DSi bajo cámara, hotspot
propio y el vídeo como plan B si la red de la sala falla.

## Otros sponsors (límite: 3 partners; aquí 0G y EF)

| Sponsor / premio | Encaje | Esfuerzo | Veredicto |
|---|---|---|---|
| Chainlink — CRE ($1.500, basta simulación) | Bajo: añadiría un orquestador de confianza que el diseño evita | 2–3 h | No |
| Arkiv — Build on Arkiv ($4.000) | Bajo: el diario ya vive en 0G; duplicarlo no aporta | 3 h | No |
| ENS — ENSv2 ($7.000) | Nombres de amigos, pero resolver exige RPC y los amigos no existen | 4 h+ | No |
| EF — Anon-RPC / alt mempool | No hay RPC que anonimizar; frame tx exige otra pila | — | No (y solo un premio EF) |
| 0G — Build with 0G Private Computer | Cuenta dentro del slot de 0G; pide "Inference on 0G Private Computer" y app que un juez pueda probar | 0 extra | Marcar solo si la decisión real sale por Private Computer |
| World, 1inch, Uniswap, Arc, Hedera, Whitechain, Definitive | DeFi/pagos/identidad; nada que ver | — | No |

Recomendación: **ninguno más**. Un tercer partner hoy cuesta horas que faltan para
cerrar los dos que sí encajan.

## Fuentes

- Premios de Mumbai (0G y resto): https://ethglobal.com/events/mumbai/prizes#0g
- EF: https://ethglobal.com/events/mumbai/prizes/ethereum-foundation
- Chainlink: https://ethglobal.com/events/mumbai/prizes/chainlink
- ENS: https://ethglobal.com/events/mumbai/prizes/ens
- Detalles de Mumbai (plazo, vídeo, jueces, IA, from scratch): https://ethglobal.com/events/mumbai/info/details
- Reglas generales: https://ethglobal.com/rules
- Estado propio: `docs/STATUS.md` (main y rama `mind`), `docs/PLAN.md`, `README.md`
- No encontrado en ninguna página: hora de inicio del hacking, tamaño de equipo,
  reglas para hardware, obligación explícita de repo público en las reglas
  generales (sí la piden 0G y EF).
