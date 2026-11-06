"""Rasterises Pixel Operator (CC0) into game/src/font_sans.c.

    python3 tools/font_sans.py PixelOperator.ttf PixelOperator-Bold.ttf > game/src/font_sans.c.new

Draws each printable ASCII glyph at 16 px (the font's own pixel grid), keeps
rows 2..14 and the advance. Paste the text-drawing functions back from git.
"""
import sys
from PIL import Image, ImageDraw, ImageFont

TOP, BOTTOM = 2, 14
print('/* Pixel Operator by Jayvee Enaguas (HarvettFox96), CC0 1.0: regular and bold,')
print(' * rasterised at 16 px by tools/font_sans.py. Printable ASCII; each glyph is')
print(' * its advance and one row bitmap per line, bit 0 = leftmost pixel. */')
print('#include "okupa_text.h"\n')
for tag, path in (('regular', sys.argv[1]), ('bold', sys.argv[2])):
    font = ImageFont.truetype(path, 16)
    adv, rows = [], []
    for c in range(32, 127):
        im = Image.new('L', (24, 20), 0)
        ImageDraw.Draw(im).text((0, 0), chr(c), font=font, fill=255)
        px = im.load()
        adv.append(int(round(font.getlength(chr(c)))))
        rows.append([sum(1 << x for x in range(24) if px[x, y] > 127) for y in range(TOP, BOTTOM + 1)])
    print('static const uint8_t %s_adv[95] = {%s};' % (tag, ', '.join(map(str, adv))))
    print('static const uint16_t %s_rows[95][%d] = {' % (tag, BOTTOM - TOP + 1))
    for c, r in enumerate(rows):
        print('    {%s}, /* %r */' % (', '.join('0x%x' % v for v in r), chr(c + 32)))
    print('};')
    print('const ok_font_sans ok_sans_%s = {%d, %d, %s_adv, %s_rows[0]};\n' % (tag, BOTTOM - TOP + 1, 13 - TOP, tag, tag))
