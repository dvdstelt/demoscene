import sys
sys.path.insert(0, '/home/dennis/Downloads/acme_303/paper/re'); sys.path.insert(0, sys.argv[1])
import numpy as np, m, rep
from PIL import Image
OUT = sys.argv[1]
M = m.load('r_after_init')
pal = np.frombuffer(rep.SEG[0x169a3:0x169a3+768], np.uint8).reshape(256, 3)
print('default pal static == mem', bytes(M[0x169a3:0x169a3+768]) == rep.SEG[0x169a3:0x169a3+768])
P8 = (pal.astype(int) * 255 // 63).clip(0,255).astype(np.uint8)
# palette strip 16x16 swatches
sw = np.zeros((16*16, 16*16, 3), np.uint8)
for i in range(256):
    y, x = divmod(i, 16)
    sw[y*16:(y+1)*16, x*16:(x+1)*16] = P8[i]
Image.fromarray(sw).resize((512, 512), Image.NEAREST).save(OUT + '/pal_default.png')
for i in range(0, 256, 8):
    print(i, [tuple(pal[i+k]) for k in range(8)])
def img(addr, w, h, name, palette=None):
    a = np.array(M[addr:addr + w*h]).reshape(h, w)
    if palette is not None:
        Image.fromarray(palette[a]).save(OUT + '/' + name)
    else:
        Image.fromarray(a).save(OUT + '/' + name)
    return a
paper = img(m.d32(M, 0x1edf5), 320, 200, 'paper_idx_gray.png')
img(m.d32(M, 0x1edf5), 320, 200, 'paper_defpal.png', P8)
Image.fromarray(((paper.astype(int) - 26) * 50).clip(0, 255).astype(np.uint8)).save(OUT + '/paper_stretched.png')
img(m.d32(M, 0x1ede1), 320, 200, 'logo_glow_75.png')
img(m.d32(M, 0x1ede5), 640, 41, 'psychic_link_text.png')
img(m.d32(M, 0x1edc5), 320, 200, 'flags.png')
print('flags nonzero', np.count_nonzero(np.array(M[m.d32(M, 0x1edc5):m.d32(M, 0x1edc5)+64000])))
# font: 29 glyphs
fb = m.d32(M, 0x1ede9)
sheet = np.zeros((32*3, 64*10), np.uint8)
widths = []
for g in range(29):
    w = M[fb + g*2048]
    widths.append(int(w))
    gl = np.array(M[fb + g*2048 + 1: fb + g*2048 + 1 + 1920]).reshape(32, 60)
    y, x = divmod(g, 10)
    sheet[y*32:(y+1)*32, x*64:x*64+60] = gl
Image.fromarray(sheet).save(OUT + '/font.png')
print('widths', widths)
