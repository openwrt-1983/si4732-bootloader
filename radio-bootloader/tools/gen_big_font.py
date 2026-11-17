#!/usr/bin/env python3
"""Generate a 1bpp GFXfont (ASCII 0x20-0x7e) from WenQuanYi Micro Hei.

LovyanGFX GFXfont bitmap rules:
  - raw 1bpp bits, row-major, MSB first, rows packed continuously (no row padding)
  - each glyph's bit stream ends with one bit OPPOSITE to its last bit
    (drawChar advances by runs, so it needs a terminator to end the last run)
  - empty glyphs still occupy one byte
  - bitmapOffset is a BYTE offset
"""
import freetype

FACE_PATH = '/usr/share/fonts/truetype/wqy/wqy-microhei.ttc'
SIZE = 44
FIRST, LAST = 0x20, 0x7e
OUT = '/home/mima1234/WorkBuddy/2026-11-17-09-36-41/radio-bootloader/FontBig.h'

face = freetype.Face(FACE_PATH, 0)
face.set_pixel_sizes(0, SIZE)

bitstream = bytearray()
glyphs = []
raw_glyph_bits = {}

for code in range(FIRST, LAST + 1):
    face.load_char(chr(code), freetype.FT_LOAD_NO_HINTING | freetype.FT_LOAD_NO_BITMAP)
    face.glyph.render(freetype.FT_RENDER_MODE_MONO)
    bm = face.glyph.bitmap
    w, h, pitch = bm.width, bm.rows, abs(bm.pitch)
    adv = face.glyph.advance.x >> 6
    xoff = face.glyph.bitmap_left
    yoff = -face.glyph.bitmap_top

    bits = []
    for y in range(h):
        row = bytes(bm.buffer[y * pitch:(y + 1) * pitch])
        for x in range(w):
            bits.append((row[x // 8] >> (7 - (x % 8))) & 1)

    raw_glyph_bits[code] = list(bits)

    if bits:
        bits.append(1 - bits[-1])   # terminator bit
    else:
        bits = [0]                  # empty glyph -> one byte
    while len(bits) % 8:
        bits.append(0)              # pad to byte boundary

    offset = len(bitstream)
    acc = 0
    for i, b in enumerate(bits):
        acc |= b << (7 - (i % 8))
        if i % 8 == 7:
            bitstream.append(acc)
            acc = 0
    assert acc == 0
    glyphs.append((offset, w, h, adv, xoff, yoff))

# ---- round-trip verification: mimic drawChar bit extraction ----
def decode(offset, w, h):
    nbytes = (w * h + 8) // 8 + 1
    flat = []
    for byte in bitstream[offset:offset + nbytes]:
        for k in range(8):
            flat.append((byte >> (7 - k)) & 1)
    return flat[:w * h]

for code, g in enumerate(glyphs, FIRST):
    off, w, h, adv, xoff, yoff = g
    got = decode(off, w, h)
    assert got == raw_glyph_bits[code], f'glyph 0x{code:02x} mismatch'
print(f'round-trip OK: {len(glyphs)} glyphs, {len(bitstream)} bitmap bytes')

def text_width(s):
    return sum(glyphs[ord(c) - FIRST][3] for c in s)

for s in ('OTA_0', 'OTA_1', '4MB', '6MB'):
    print(f'  width({s}) = {text_width(s)} px')

assert text_width('OTA_0') <= 124 and text_width('OTA_1') <= 124

with open(OUT, 'w', encoding='utf-8') as f:
    f.write('/* FontBig: WenQuanYi Micro Hei, 44px, 1bpp GFXfont, ASCII 0x20-0x7e.\n')
    f.write(' * Generated for the ESP32-S3 boot selector. Use with lcd.setFont(&FontBig).\n */\n')
    f.write('#pragma once\n#include <LovyanGFX.hpp>\n\n')
    f.write(f'const uint8_t FontBigBitmaps[] PROGMEM = {{\n')
    for i in range(0, len(bitstream), 16):
        f.write('  ' + ', '.join(f'0x{b:02x}' for b in bitstream[i:i + 16]) + ',\n')
    f.write('};\n\n')
    f.write('const lgfx::GFXglyph FontBigGlyphs[] PROGMEM = {\n')
    for code, g in enumerate(glyphs, FIRST):
        off, w, h, adv, xoff, yoff = g
        label = chr(code).replace('\\', '\\\\').replace("'", "\\'")
        f.write(f'  {{ {off:5d}, {w:2d}, {h:2d}, {adv:2d}, {xoff:3d}, {yoff:3d} }}, // 0x{code:02x} \'{label}\'\n')
    f.write('};\n\n')
    f.write('const lgfx::GFXfont FontBig PROGMEM = {\n')
    f.write('  (uint8_t*)FontBigBitmaps,\n')
    f.write('  (lgfx::GFXglyph*)FontBigGlyphs,\n')
    f.write(f'  0x20, 0x7e, {SIZE + 2} }};\n')
print('wrote', OUT)
