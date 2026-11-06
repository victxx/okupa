"""Turns the 2x2 sheet of painted skies into game/src/sky_art.c: four skies
(dawn, day, sunset, night) of 160x96 DS colours, drawn twice that size with
a little sideways parallax.

    python3 tools/sky.py local/art/sky-sheet-1.png > game/src/sky_art.c
"""
import sys
from PIL import Image

sheet = Image.open(sys.argv[1]).convert('RGB')
w, h = sheet.size
print('/* Four painted skies (dawn, day, sunset, night), 160x96 DS colours each.')
print(' * Made by tools/sky.py from a sheet generated with ChatGPT (see')
print(' * tools/art/sky-sheet-prompt.txt and THIRD_PARTY.md). */')
print('#include "okupa_house.h"\n')
print('const uint16_t ok_sky_art[4][96 * 160] = {')
preview = Image.new('RGB', (160, 96 * 4))
for k in range(4):
    x0, y0 = k % 2 * w // 2, k // 2 * h // 2
    side = w // 2
    band = side * 96 // 160  # the panel's full width, its lower part (the horizon of clouds)
    top = y0 + side * 9 // 100 if k == 3 else y0 + side - band - 2  # night: high enough to keep the moon
    panel = sheet.crop((x0 + 2, top, x0 + side - 2, top + band)).resize((160, 96), Image.BOX)
    preview.paste(panel, (0, 96 * k))
    px = panel.load()
    row = []
    for y in range(96):
        for x in range(160):
            r, g, b = px[x, y]
            row.append('0x%04x' % (0x8000 | (r * 31 + 127) // 255 | ((g * 31 + 127) // 255) << 5 | ((b * 31 + 127) // 255) << 10))
    print('    {' + ', '.join(row) + '},')
print('};')
preview.resize((480, 96 * 12), Image.NEAREST).save('build/preview/sky-art.png')
