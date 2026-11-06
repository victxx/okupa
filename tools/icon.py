"""Re-encodes the menu icon for ndstool's DSi animated banner.

    python3 tools/icon.py local/submission-art/okupa-icon.gif platform/dsi

ndstool takes each GIF frame as it is stored and that frame's own colour
table, so the frames are written whole (not as changes over the last one),
each with the same 16 colours as a local table, and the see-through
colour first (index 0 is transparent on the DS). The first frame is also
saved as icon.png for the plain DS banner.
"""
import sys
from PIL import Image

src, out = sys.argv[1], sys.argv[2]
gif = Image.open(src)
frames, durations = [], []
for i in range(gif.n_frames):
    gif.seek(i)
    frames.append(gif.convert('RGBA'))
    durations.append(gif.info.get('duration', 100))
colours = []
for f in frames:
    for r, g, b, a in f.getdata():
        if a >= 128 and (r, g, b) not in colours:
            colours.append((r, g, b))
assert len(colours) <= 15, 'the DS icon has 15 colours and see-through'
palette = [(255, 0, 255)] + colours
flat = [c for rgb in palette for c in rgb] + [0] * (3 * (256 - len(palette)))
paletted = []
for f in frames:
    p = Image.new('P', f.size)
    p.putpalette(flat)
    p.putdata([0 if a < 128 else palette.index((r, g, b)) for r, g, b, a in f.getdata()])
    paletted.append(p)


def lzw(pixels):
    """Uncompressed LZW: 5-bit codes, a clear code every 12, so the table never grows."""
    codes, bits, nbits = [], 0, 0
    data = bytearray()
    for i, v in enumerate(pixels):
        if i % 12 == 0:
            codes.append(16)
        codes.append(v)
    codes.append(17)
    for c in codes:
        bits |= c << nbits
        nbits += 5
        while nbits >= 8:
            data.append(bits & 255)
            bits >>= 8
            nbits -= 8
    if nbits:
        data.append(bits & 255)
    blocks = bytearray([4])  # minimum code size
    for i in range(0, len(data), 255):
        blocks += bytes([len(data[i:i + 255])]) + data[i:i + 255]
    return bytes(blocks + b'\0')


# Pillow crops each frame to what changed, which ndstool does not undo: so a
# GIF by hand, every frame whole, every frame with its own colour table.
table = bytes(c for rgb in palette for c in rgb) + bytes(3 * (16 - len(palette)))
w, h = frames[0].size
gif_out = bytearray(b'GIF89a' + bytes([w, 0, h, 0, 0, 0, 0]))
gif_out += b'\x21\xff\x0bNETSCAPE2.0\x03\x01\x00\x00\x00'
for p, ms in zip(paletted, durations):
    cs = (ms + 5) // 10
    gif_out += bytes([0x21, 0xf9, 4, (1 << 2) | 1, cs & 255, cs >> 8, 0, 0])  # keep, index 0 see-through
    gif_out += bytes([0x2c, 0, 0, 0, 0, w, 0, h, 0, 0x80 | 3])  # a local table of 16 colours
    gif_out += table + lzw(list(p.getdata()))
gif_out += b'\x3b'
open(out + '/icon.gif', 'wb').write(gif_out)
paletted[0].save(out + '/icon.png', transparency=0)
print(len(frames), 'frames,', len(colours), 'colours')


# The whole DSi banner (version 0x103), handed to ndstool with -t: its own
# GIF reader fills only the first frame's palette and drops frames, so the
# animation is laid out here. 4 bits a pixel in 8x8 tiles, BGR555 colours,
# index 0 see-through; the sequence is a frame (1/60 s), bitmap, palette.
def tiles(p):
    px = list(p.getdata())
    data = bytearray()
    for ty in range(4):
        for tx in range(4):
            for y in range(8):
                for x in range(0, 8, 2):
                    a = px[(ty * 8 + y) * 32 + tx * 8 + x]
                    b = px[(ty * 8 + y) * 32 + tx * 8 + x + 1]
                    data.append(a | b << 4)
    return bytes(data)


def bgr555(rgb):
    r, g, b = rgb
    return r >> 3 | (g >> 3) << 5 | (b >> 3) << 10  # as ndstool converts


def crc16(data):
    crc = 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return crc


pal = b''.join(bgr555(c).to_bytes(2, 'little') for c in palette) + bytes(32 - 2 * len(palette))
banner = bytearray(0x23C0)
banner[0:2] = (0x0103).to_bytes(2, 'little')
banner[0x20:0x220] = tiles(paletted[0])
banner[0x220:0x240] = pal
title = 'Okupa\nA tiny squatter\nvictxx'.encode('utf-16-le')
for lang in range(8):
    banner[0x240 + lang * 0x100:0x240 + lang * 0x100 + len(title)] = title
unique, seq = [], []
for p, ms in zip(paletted, durations):
    t = tiles(p)
    if t not in unique:
        unique.append(t)
    seq.append(max(1, min(255, (ms * 60 + 500) // 1000)) | unique.index(t) << 8)  # palette 0 for all
assert len(unique) <= 8 and len(seq) <= 64
for i, t in enumerate(unique):
    banner[0x1240 + i * 512:0x1240 + i * 512 + 512] = t
banner[0x2240:0x2260] = pal
for i, token in enumerate(seq):
    banner[0x2340 + i * 2:0x2342 + i * 2] = token.to_bytes(2, 'little')
for at, end in ((2, 0x840), (4, 0x940), (6, 0xA40)):
    banner[at:at + 2] = crc16(banner[0x20:end]).to_bytes(2, 'little')
banner[8:10] = crc16(banner[0x1240:0x23C0]).to_bytes(2, 'little')
open(out + '/banner.bin', 'wb').write(banner)
print(len(unique), 'pictures in a', len(seq), 'step loop')
