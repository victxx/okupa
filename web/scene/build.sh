#!/bin/sh
# Builds web/public/scene.wasm from the console's own renderer. Needs clang with the wasm32
# target (Apple clang has it) and wasm-ld (brew install lld).
set -e
cd "$(dirname "$0")/../.."
LD=${WASM_LD:-$(command -v wasm-ld || echo /opt/homebrew/opt/lld/bin/wasm-ld)}
OUT=build/web/scene; mkdir -p $OUT web/public
SRC="web/scene/scene.c game/src/house.c game/src/monkey.c game/src/r3d.c game/src/room_skin.c game/src/outside_skin.c game/src/ivy_art.c game/src/sky_art.c game/src/plant_art.c core/src/act.c"
for f in $SRC; do
  clang --target=wasm32 -std=c99 -O2 -ffreestanding -fno-builtin -nostdlibinc -isystem web/scene/include \
    -isystem "$(clang -print-resource-dir)/include" -Icore/include -Igame/include -c $f -o $OUT/$(basename $f .c).o
done
"$LD" --no-entry --export=scene_init --export=scene_pic --export=scene_rgba --export=scene_hang --export=scene_event --export=scene_move --export=scene_step --export=scene_draw --strip-all -z stack-size=262144 --initial-memory=4194304 -o web/public/scene.wasm $OUT/*.o
ls -l web/public/scene.wasm
