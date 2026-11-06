"""Cuts the hanging ivy sprites out of a sheet on a magenta background.

    python3 tools/ivy.py local/art/ivy-sheet-1.png > game/src/ivy_art.c

Magenta becomes see-through (texel 0, bit 15 clear). Each vine is found by
the columns it covers, cropped to its own box, hung from its top, and
scaled down to 32x64 texels of DS colours (BGR555, bit 15 set).
"""
import sys
from PIL import Image

N, TW, TH = 6, 32, 64
sheet = Image.open(sys.argv[1]).convert('RGB')
w, h = sheet.size
px = sheet.load()


def keyed(c):
    r, g, b = c
    return r > 150 and b > 150 and g < 110 and abs(r - b) < 90


mask = [[not keyed(px[x, y]) for x in range(w)] for y in range(h)]
cols = [sum(mask[y][x] for y in range(h)) for x in range(w)]
# Runs of covered columns, the N widest are the vines.
runs, x = [], 0
while x < w:
    if cols[x] > 2:
        x0 = x
        while x < w and cols[x] > 2:
            x += 1
        runs.append((x0, x))
    x += 1
runs = sorted(sorted(runs, key=lambda r: r[1] - r[0], reverse=True)[:N])
if len(runs) < N:
    sys.exit('found %d vines, want %d' % (len(runs), N))

out = Image.new('RGBA', (TW * N, TH), (0, 0, 0, 0))
for i, (x0, x1) in enumerate(runs):
    ys = [y for y in range(h) if any(mask[y][x] for x in range(x0, x1))]
    y0, y1 = ys[0], ys[-1] + 1
    crop = Image.new('RGBA', (x1 - x0, y1 - y0))
    cp = crop.load()
    for y in range(y0, y1):
        for x in range(x0, x1):
            cp[x - x0, y - y0] = px[x, y] + ((255,) if mask[y][x] else (0,))
    scale = min(TW / crop.width, TH / crop.height)
    tw, th = max(1, round(crop.width * scale)), max(1, round(crop.height * scale))
    small = crop.resize((tw, th), Image.BOX)
    out.paste(small, ((TW - tw) // 2 + i * TW, 0))

op = out.load()
print('/* The hanging ivy: %d sprites of %dx%d texels side by side, DS colours,' % (N, TW, TH))
print(' * 0 = see-through. Made by tools/ivy.py from a sheet generated with')
print(' * ChatGPT (see tools/art/ivy-sheet-prompt.txt and THIRD_PARTY.md). */')
print('#include "okupa_house.h"\n')
print('const uint16_t ok_ivy_art[%d * %d] = {' % (TW * N, TH))
for y in range(TH):
    row = []
    for x in range(TW * N):
        R, G, B, A = op[x, y]
        if A < 140:
            row.append('0')
        else:
            row.append('0x%04x' % (0x8000 | (R * 31 + 127) // 255 | ((G * 31 + 127) // 255) << 5 | ((B * 31 + 127) // 255) << 10))
    print('    ' + ', '.join(row) + ',')
print('};')
prev = Image.new('RGB', (TW * N, TH), (40, 40, 44))
prev.paste(out, (0, 0), out)
prev.resize((TW * N * 4, TH * 4), Image.NEAREST).save('build/preview/ivy-art.png')
