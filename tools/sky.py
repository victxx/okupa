"""Turns four ChatGPT sky pictures into the outside's 360 degree panoramas.

    python3 tools/sky.py local/art/sky-dawn.png local/art/sky-day.png \
        local/art/sky-dusk.png local/art/sky-night.png > game/src/sky_art.c

Each picture is scaled to half the panorama's width and mirrored, so the
two ends meet without a seam, then reduced to 256 colours (one byte a
texel, a DS-colour palette per mood). Rows run from 80 degrees up (top)
to 40 below the horizon (bottom); the pictures have the horizon in their
lower third, where it lands. Order: dawn, day, dusk, night (OK_SKY_*).
"""
import sys
from PIL import Image

W, H = 512, 256
print('/* The outside\'s four skies (dawn, day, dusk, night): 360 degree panoramas')
print(' * of %dx%d texels, one palette index each, and a palette of DS colours per' % (W, H))
print(' * sky. Made by tools/sky.py from pictures generated with ChatGPT (see')
print(' * tools/art/sky-panorama-prompt.txt and THIRD_PARTY.md). */')
print('#include "okupa_house.h"\n')
pals, idx = [], []
for path in sys.argv[1:5]:
    im = Image.open(path).convert('RGB').resize((W // 2, H), Image.LANCZOS)
    full = Image.new('RGB', (W, H))
    full.paste(im, (0, 0))
    full.paste(im.transpose(Image.FLIP_LEFT_RIGHT), (W // 2, 0))
    q = full.quantize(256, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    pal = q.getpalette()[:768]
    pal += [0] * (768 - len(pal))
    pals.append(pal)
    idx.append(list(q.getdata()))
    q.convert('RGB').save('build/preview/sky-%d.png' % len(idx))
print('const uint16_t ok_sky_palette[4][256] = {')
for pal in pals:
    row = []
    for i in range(256):
        R, G, B = pal[i * 3:i * 3 + 3]
        row.append('0x%04x' % (0x8000 | (R * 31 + 127) // 255 | ((G * 31 + 127) // 255) << 5 | ((B * 31 + 127) // 255) << 10))
    print('    {' + ', '.join(row) + '},')
print('};\n')
print('const uint8_t ok_sky_art[4][%d * %d] = {' % (W, H))
for data in idx:
    print('    {')
    for y in range(H):
        print('        ' + ','.join(str(v) for v in data[y * W:(y + 1) * W]) + ',')
    print('    },')
print('};')
