// The okupa's attic, live: the console's own renderer (game/src/house.c, monkey.c, r3d.c)
// compiled to WebAssembly by web/scene/build.sh. 256x192 like the DSi's top screen, ticked at
// 60 Hz like the console, lit by this computer's clock.
const EVENT = { FEED: 0, PLAY: 1, PET: 2, REFUSE: 3, HELLO: 4 };
const POSE = { IDLE: 0, HAPPY: 1 };

export async function startScene(canvas) {
  const { instance } = await WebAssembly.instantiateStreaming(fetch('/scene.wasm'), {});
  const x = instance.exports;
  x.scene_init(0x6f6b7570 ^ (Date.now() & 0xffff), 0, 0, 0, 0);
  const g = canvas.getContext('2d'), img = g.createImageData(256, 192);
  let last = performance.now(), acc = 0, happy = 0;

  function frame(now) {
    acc = Math.min(acc + now - last, 250); last = now;
    while (acc >= 1000 / 60) { x.scene_step(happy > 0 ? POSE.HAPPY : POSE.IDLE); if (happy > 0) happy--; acc -= 1000 / 60; }
    const d = new Date();
    const ptr = x.scene_draw(d.getHours(), d.getMinutes(), 0);
    img.data.set(new Uint8Array(x.memory.buffer, ptr, 256 * 192 * 4));
    g.putImageData(img, 0, 0);
    requestAnimationFrame(frame);
  }
  requestAnimationFrame(frame);

  canvas.addEventListener('click', () => { x.scene_event(EVENT.PET); happy = 120; });

  return {
    // A 64x64 one-bit picture (512 B) into one of the attic's six frames.
    hang(slot, pixels1bpp, animate) {
      new Uint8Array(x.memory.buffer, x.scene_pic(), 512).set(pixels1bpp);
      x.scene_hang(slot, animate ? 1 : 0);
    },
    cheer() { x.scene_event(EVENT.HELLO); happy = 240; },
  };
}
