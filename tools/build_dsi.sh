#!/bin/sh
# Builds build/dsi/okupa.nds in the BlocksDS image pinned by digest in
# platform/dsi/toolchain.lock.json. The source is mounted read-only; the
# container has no network and writes only to build/dsi.
set -eu
cd "$(dirname "$0")/.."
image=$(sed -n 's/.*"image": "\(.*\)".*/\1/p' platform/dsi/toolchain.lock.json)
id=$(git rev-parse --short=7 HEAD)$(git diff --quiet HEAD -- . 2>/dev/null || echo -dirty)
mkdir -p build/dsi
rm -f build/dsi/obj/platform/dsi/main.o # it carries the build id
# The opening film, baked on this computer (make film), goes inside the ROM.
rm -rf build/dsi/nitro
if [ -f build/film.okf ]; then mkdir -p build/dsi/nitro && cp build/film.okf build/dsi/nitro/film.okf; fi
docker run --rm --network none --read-only --tmpfs /tmp --cap-drop ALL \
    --security-opt no-new-privileges --user "$(id -u):$(id -g)" \
    --mount "type=bind,src=$PWD,dst=/work,readonly" \
    --mount "type=bind,src=$PWD/build/dsi,dst=/work/build/dsi" \
    --workdir /work --entrypoint sh "$image" -c \
    "make -f platform/dsi/Makefile BUILD_ID=$id R3D_EXTRA=\"${R3D_EXTRA:-}\" R3D_GL=${R3D_GL:-1} -j4 && \
     \$WONDERFUL_TOOLCHAIN/toolchain/gcc-arm-none-eabi/bin/arm-none-eabi-size build/dsi/okupa.elf"
shasum -a 256 build/dsi/okupa.nds
