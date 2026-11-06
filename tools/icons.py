"""Cuts the buttons' icons out of a ChatGPT sheet (4x3 grid on flat magenta)
into game/src/icons_art.c: 32x32 colours (DS BGR555) and coverage 0..4.

    python3 tools/icons.py local/art/icon-sheet-1.png > game/src/icons_art.c

Each cell's object is found by its distance from magenta, cropped square
with a margin, and averaged down; pixels on the magenta fringe count as
partly covered and lend no colour.
"""
import sys
from PIL import Image

NAMES = ['feed', 'play', 'pet', 'sleep', 'wake', 'draw', 'photo', 'mail', 'dev', 'wear', 'home', 'music']
sheet = Image.open(sys.argv[1]).convert('RGB')
W, H = sheet.size
px = sheet.load()

def solid(c):
    r, g, b = c
    return (255 - r) ** 2 + g * g * 2 + (255 - b) ** 2 > 140 ** 2  # far from magenta

cells, preview = [], Image.new('RGB', (32 * 12, 32), (255, 250, 235))
for k in range(12):
    cx0, cy0 = k % 4 * W // 4, k // 4 * H // 3
    cx1, cy1 = cx0 + W // 4, cy0 + H // 3
    xs, ys = [], []
    for y in range(cy0 + 4, cy1 - 4, 2):
        for x in range(cx0 + 4, cx1 - 4, 2):
            if solid(px[x, y]):
                xs.append(x)
                ys.append(y)
    x0, x1, y0, y1 = min(xs), max(xs), min(ys), max(ys)
    side = int(max(x1 - x0, y1 - y0) * 1.08) + 4
    mx, my = (x0 + x1) // 2, (y0 + y1) // 2
    crop = Image.new('RGB', (side, side), (255, 0, 255))  # outside the sheet is background too
    crop.paste(sheet.crop((max(cx0, mx - side // 2), max(cy0, my - side // 2), min(cx1, mx - side // 2 + side),
                           min(cy1, my - side // 2 + side))),
               (max(cx0, mx - side // 2) - (mx - side // 2), max(cy0, my - side // 2) - (my - side // 2)))
    big = crop.resize((256, 256), Image.LANCZOS)
    bp = big.load()
    colour, cover = [], []
    for y in range(32):
        for x in range(32):
            n, r, g, b = 0, 0, 0, 0
            for dy in range(8):
                for dx in range(8):
                    c = bp[x * 8 + dx, y * 8 + dy]
                    if solid(c):
                        n += 1
                        r, g, b = r + c[0], g + c[1], b + c[2]
            a = (n * 4 + 32) // 64
            cover.append(a)
            if n:
                r, g, b = r // n, g // n, b // n
                colour.append(0x8000 | (r * 31 + 127) // 255 | ((g * 31 + 127) // 255) << 5 | ((b * 31 + 127) // 255) << 10)
                bg = (255, 250, 235)
                preview.putpixel((k * 32 + x, y), tuple((v * a + w * (4 - a)) // 4 for v, w in zip((r, g, b), bg)))
            else:
                colour.append(0)
    cells.append((colour, cover))
preview.resize((32 * 12 * 4, 32 * 4), Image.NEAREST).save('build/preview/icons-gpt.png')
print('/* The buttons\' icons, cut by tools/icons.py from a sheet generated with')
print(' * ChatGPT (tools/art/icon-sheet-prompt.txt, THIRD_PARTY.md): 32x32 DS colours')
print(' * and a coverage of 0..4 per pixel. Order: ' + ', '.join(NAMES) + '. */')
print('#include "okupa_icons.h"\n')
print('const uint16_t ok_icon_colour[OK_ICONS][OK_ICON_SIZE * OK_ICON_SIZE] = {')
for colour, _ in cells:
    print('    {' + ', '.join('0x%04x' % v for v in colour) + '},')
print('};')
print('const uint8_t ok_icon_cover[OK_ICONS][OK_ICON_SIZE * OK_ICON_SIZE] = {')
for _, cover in cells:
    print('    {' + ', '.join(str(v) for v in cover) + '},')
print('};')
