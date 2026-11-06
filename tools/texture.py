"""Turns a 4x4 texture sheet into the attic's 128x128 skin of DS colours.

    python3 tools/texture.py local/art/room-sheet-1.png > game/src/room_skin.c

Each tile is cut a little inside its edges, averaged down to 32x32 and
rounded to 5 bits a channel (BGR555, bit 15 set). Order: row by row, as the
OK_ROOM_* names in okupa_house.h.
"""
import sys
from PIL import Image, ImageEnhance

sheet = Image.open(sys.argv[1]).convert('RGB')
w, h = sheet.size
out = Image.new('RGB', (128, 128))
for r in range(4):
    for c in range(4):
        x0, y0, x1, y1 = c * w / 4, r * h / 4, (c + 1) * w / 4, (r + 1) * h / 4
        tile = sheet.crop((int(x0 + 3), int(y0 + 3), int(x1 - 3), int(y1 - 3))).resize((32, 32), Image.BOX)
        tile = ImageEnhance.Color(ImageEnhance.Contrast(tile).enhance(1.08)).enhance(1.05)
        out.paste(tile, (c * 32, r * 32))
px = out.load()
print('/* The attic\'s textures: 16 tiles of 32x32 in a 128x128 skin, DS colours.')
print(' * Made by tools/texture.py from a sheet generated with ChatGPT (see')
print(' * tools/art/room-sheet-prompt.txt and THIRD_PARTY.md). */')
print('#include "okupa_house.h"\n')
print('const uint16_t ok_room_skin[128 * 128] = {')
for y in range(128):
    row = []
    for x in range(128):
        R, G, B = px[x, y]
        row.append('0x%04x' % (0x8000 | (R * 31 + 127) // 255 | ((G * 31 + 127) // 255) << 5 | ((B * 31 + 127) // 255) << 10))
    print('    ' + ', '.join(row) + ',')
print('};')
out.resize((512, 512), Image.NEAREST).save('build/preview/room-skin.png')
