# The okupa's look and sound

The okupa is a little monkey in a squatted attic. Everything is drawn and
played by our own C code (no third-party art or music):

| Piece | Code | Preview |
|---|---|---|
| 3D renderer: triangles, depth, four-tone toon light, outlines, textures | `game/src/r3d.c` | all renders |
| The monkey: skin texture, clothes, springs and behaviours | `game/src/monkey.c` | `build/okupa-monkey` → `monkey-*.png` |
| The attic: light through the day, sunbeam, string lights, the drawing on the wall | `game/src/house.c` | `build/okupa-house` → `house-*.png` |
| Screens: labels over the attic, buttons, closet | `game/src/ui.c` | `build/okupa-preview` → `home-*.png`, `closet.png` |
| Music and sounds | `game/src/music.c`, `platform/dsi/audio.c` | `build/okupa-music` → `okupa-music.wav` |
| The splash and the opening film | `game/src/intro.c` (outside: `ok_house_outside` in `house.c`) | `build/okupa-intro <dir>` → frames |
| Gallery frames (6 in the attic, 6 on the terrace) | `ok_house_frame(slot, pixels)` in `house.c` | `house-*.png` |

`make preview` writes all of them to `build/preview/`. `build/okupa-house <dir>
<frames dir>` also writes a short film, one PNG per 1/20 s.

## Software 3D, not the DSi's 3D engine

The top screen is drawn by `r3d.c` on the CPU, not by the DSi's 3D hardware.

- **The same pixels everywhere.** The host previews, the tests and a future
  phone port run the very same code as the console. With the hardware engine,
  the host would need a second renderer that imitates it, and the two would drift.
- **Its look is not one the hardware gives for free.** Each object has its own
  four tones and outline colour. The hardware has one toon table and eight edge
  colours for everything. The eyes and mouth are masked to the head and the
  smiley's face follows the mask's plane. Both need the depth and object
  buffers, which the CPU cannot read back from the hardware.
- **It is cheap enough.** The still attic is drawn once per change of light
  and kept (`ok_r3d_keep`). Each frame redraws only the okupa (about 1200
  triangles in its bounding box). Divisions are per triangle, not per pixel.
  The host draws a frame in about 0.1 ms. Dev → "Frame: ... ms to draw" shows
  the real figure on the console.

If that figure is too high on the DSi, the scene code can drive the hardware
instead (`ok_r3d_*` is already shaped like it: transforms, materials,
triangles). That stays a fallback, not the plan.

## Moving like it is alive

Every joint is a damped spring (`ok_monkey_step`, 60 ticks a second) that eases
towards the target of the moment, so poses blend instead of snapping. Hops
anticipate, overshoot and squash on landing. Tail joints, ears and the head
each follow a little later. What it does when left alone is drawn from a
seeded random sequence weighted by its mood: look around, scratch, stretch,
wander to the window, hop, sigh, wave, or peek from behind the smiley mask.
Feeding (bites, chewing), play (tossing a ball), petting (hearts), refusing
(a head shake) and bedtime (it walks to the mattress and lies down) are their
own moves. The console ticks it in real time, even when a frame takes longer
than 1/60 s.

## Music

One original eight-bar tune in F ("Attic morning") in three arrangements:
- **day:** marimba, FM electric piano, bass, soft drums, swung
- **night:** music box and slower piano
- **sleeping:** a lullaby of bells

The sound effects are made by the same synthesiser. On the DSi a looping
16-bit buffer is refilled just ahead of the speaker. Its rate and the CPU
timer share a crystal (2048 ticks per sample), so they never drift apart.

## Opening, garden, gallery

The console opens on a splash: OKUPA stamped over a turning sun of halftone
dots, "by victorxva" typed in, a shine across the name, and a bell jingle.
Then a short film plays, the cottage on its floating island at night, the
camera flying in through the round attic window. A touch skips either. The
outside is the attic seen from outside: the same walls, roof, round window,
door and side window.

The sky outside is a low-poly dome drawn round the camera, like a tiny 360
degree panorama, so it turns with the camera and the stars stay put. It has
four moods: dawn, day, dusk and night, blended by the clock. The facets shade
from the horizon to the zenith, and warm towards the sun at dawn and dusk.
Flat two-tone clouds drift slowly round it, and a sea of them lies below the
island. The sun or the moon sits on the dome, and at night there are stars.
The island is an irregular rock: a lawn, a rounded lip, and cliffs narrowing
to a point. Small bushes grow on it (no trees), and ivy sprites hang over its
edge (cut out of a ChatGPT sheet by `tools/ivy.py`, see THIRD_PARTY.md).

"Out" takes the okupa to the terrace, where six more frames hang (three on
the brick wall, three on easels). Frames stay empty until `ok_house_frame`
hangs a 64x64 picture in them: they hold `ok_ui.wall`, the drawings the
console saw included in a block, oldest first. A new one is hung with a
little show: it drops onto its nail, bumps, swings and settles, with dust and
glints, while the okupa cheers (it walks in first if it was outside).
