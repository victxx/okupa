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

## 3D: software on the host, the DS's own 3D hardware on the console

`r3d.c` builds every shape on the CPU in the same way everywhere: the
primitives, the lighting into four tones, and the outlines between objects.
The host and the tests rasterise in software, pixel for pixel. The console
build (`OK_R3D_GL`, after `ok_r3d_hardware(1)`) sends the same triangles to
the DS's 3D hardware instead.

- **The vertices go in world space, with their tone as the colour.** The
  camera is the hardware's matrix, against one fixed frustum. Textures are
  the uv rectangles, uploaded once each and small (16 or 32 texels).
- **Still scenes are recorded once and replayed.** Everything drawn after
  `ok_r3d_begin` is recorded as GPU commands. A kept frame (`ok_r3d_keep`)
  is replayed from the CPU straight into the GX FIFO each frame, and only
  the okupa is built anew. The opening film builds its island once and draws
  it again under each new camera (`ok_r3d_world_keep`/`ok_r3d_world_again`).
- **Three layers on the top screen.** The game's 2D layer is BG3, drawn over
  the 3D and see-through where bit 15 is clear. The 3D is BG0. The sky is
  BG2, a 512x256 256-colour panorama that the hardware turns and scales
  with the camera, so it costs no CPU.
- **On the hardware, hazes, glows and the sunbeam are skipped.** They read
  back the software renderer's depth, which the hardware does not give back.
- **Before the 3D moves to the hardware, the icons are drawn once in
  software.** They need the software renderer.

Measured in melonDS 1.1, DS mode (67 MHz, half the DSi's clock), with its
emulated Wi-Fi. The software renderer drew the film in about 440 ms a
frame. On the hardware the film draws in about 30 ms and the splash in about
30 ms. The real limit now is the network's cooperative threads: they held
the CPU for 100 to 350 ms between yields in these runs. The ROM shows
"N fps draw D loop L ms" at the bottom of the top screen while R is held.
`make perf` counts the work per frame on the host.

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
