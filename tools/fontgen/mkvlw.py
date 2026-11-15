# Convert a TTF/OTF font to a smooth (anti-aliased) VLW font as a C array
# Usage: python3 tools/fontgen/mkvlw.py firmware/nova/Fonts.h   (needs: pip install freetype-py, and the Inter fonts)
# INTER_DIR points to the folder with the Inter .otf files (https://github.com/rsms/inter).
import freetype, os, struct, sys

def vlw(path, px, chars, track=0):
    face = freetype.Face(path)
    face.set_pixel_sizes(0, px)
    glyphs = []
    for ch in sorted(set(chars)):
        face.load_char(ch, freetype.FT_LOAD_RENDER | freetype.FT_LOAD_TARGET_LIGHT)
        g = face.glyph; bm = g.bitmap
        w, h = bm.width, bm.rows
        data = bytes(bm.buffer) if w and h else b''
        if bm.pitch != w and w:
            data = b''.join(bytes(bm.buffer[r*bm.pitch:r*bm.pitch+w]) for r in range(h))
        adv = (g.advance.x + 32) >> 6
        if track and ('A' <= ch <= 'Z' or '0' <= ch <= '9'): adv += track   # capitals read better with a little air
        glyphs.append((ord(ch), h, w, adv, g.bitmap_top, g.bitmap_left, data))
    asc = face.size.ascender >> 6; desc = -(face.size.descender >> 6)
    # ascent of 'd', descent of 'p' as in the Processing format
    face.load_char('d'); a_d = face.glyph.bitmap_top
    face.load_char('p'); d_p = face.glyph.bitmap.rows - face.glyph.bitmap_top
    out = struct.pack('>6i', len(glyphs), 11, px, 0, a_d, d_p)
    for (u, h, w, adv, top, left, _) in glyphs:
        out += struct.pack('>7i', u, h, w, adv, top, left, 0)
    for g in glyphs: out += g[6]
    return out

def carray(name, data):
    lines = []
    for i in range(0, len(data), 24):
        lines.append('  ' + ','.join('0x%02X' % b for b in data[i:i+24]) + ',')
    return 'static const uint8_t %s[] PROGMEM = {\n%s\n};\n' % (name, '\n'.join(lines))

ascii_ = ''.join(chr(c) for c in range(32, 127)) + '°±µ·–—…'
digits = '0123456789.,:-+ kHzM'
I = os.environ.get('INTER_DIR', '/usr/share/fonts/opentype/inter') + '/'
specs = [
  ('font_body',     I + 'Inter-Medium.otf',           13, ascii_, 1),
  ('font_body_b',   I + 'Inter-Bold.otf',             13, ascii_, 1),
  ('font_title',    I + 'Inter-Bold.otf',             16, ascii_, 1),
  ('font_large',    I + 'Inter-ExtraBold.otf',        22, ascii_, 1),
  ('font_caption',  I + 'Inter-SemiBold.otf',         11, ascii_, 1),
  ('font_freq',     I + 'InterDisplay-SemiBold.otf',  44, digits, 0),
  ('font_freq_s',   I + 'InterDisplay-SemiBold.otf',  30, digits, 0),
]
src = '// Inter typeface (SIL Open Font License 1.1), rendered as smooth VLW fonts\n#pragma once\n#include <stdint.h>\n#ifndef PROGMEM\n#define PROGMEM\n#endif\n\n'
total = 0
for name, path, px, chars, track in specs:
    d = vlw(path, px, chars, track); total += len(d)
    src += carray(name, d) + '\n'
open(sys.argv[1], 'w').write(src)
print('bytes', total)
