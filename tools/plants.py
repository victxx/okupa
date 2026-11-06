"""Cuts the two plant sprites (a houseplant, a bush) out of a sheet on magenta.

    python3 tools/plants.py local/art/plant-sheet-1.png > game/src/plant_art.c

Each half of the sheet holds one; magenta becomes see-through (texel 0),
the plant is cropped to its box, stood on the bottom edge and scaled to
32x32 texels of DS colours (BGR555, bit 15 set). They are drawn as two
crossed quads, the old trick for leaves.
"""
import sys
from PIL import Image

N, T = 2, 32
sheet = Image.open(sys.argv[1]).convert('RGB')
w, h = sheet.size
px = sheet.load()


def keyed(c):
    r, g, b = c
    return r > 150 and b > 150 and g < 110 and abs(r - b) < 90


out = Image.new('RGBA', (T * N, T), (0, 0, 0, 0))
for i in range(N):
    x0, x1 = i * w // N, (i + 1) * w // N
    xs, ys = [], []
    for y in range(h):
        for x in range(x0, x1):
            if not keyed(px[x, y]):
                xs.append(x)
                ys.append(y)
    bx0, bx1, by0, by1 = min(xs), max(xs) + 1, min(ys), max(ys) + 1
    crop = Image.new('RGBA', (bx1 - bx0, by1 - by0))
    cp = crop.load()
    for y in range(by0, by1):
        for x in range(bx0, bx1):
            c = px[x, y]
            cp[x - bx0, y - by0] = c + ((0,) if keyed(c) else (255,))
    s = min(T / crop.width, T / crop.height)
    tw, th = max(1, round(crop.width * s)), max(1, round(crop.height * s))
    small = crop.resize((tw, th), Image.BOX)
    out.paste(small, (i * T + (T - tw) // 2, T - th))

op = out.load()
print('/* Two plant sprites of %dx%d texels side by side (a houseplant, a bush),' % (T, T))
print(' * DS colours, 0 = see-through. Made by tools/plants.py from a sheet')
print(' * generated with ChatGPT (see THIRD_PARTY.md). */')
print('#include "okupa_house.h"\n')
print('const uint16_t ok_plant_art[%d * %d] = {' % (T * N, T))
for y in range(T):
    row = []
    for x in range(T * N):
        R, G, B, A = op[x, y]
        row.append('0' if A < 140 else '0x%04x' % (0x8000 | (R * 31 + 127) // 255 | ((G * 31 + 127) // 255) << 5 | ((B * 31 + 127) // 255) << 10))
    print('    ' + ', '.join(row) + ',')
print('};')
prev = Image.new('RGB', (T * N, T), (40, 40, 44))
prev.paste(out, (0, 0), out)
prev.resize((T * N * 6, T * 6), Image.NEAREST).save('build/preview/plant-art.png')
