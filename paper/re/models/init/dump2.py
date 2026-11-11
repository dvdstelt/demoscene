import sys
sys.path.insert(0, '/home/dennis/Downloads/acme_303/paper/re'); sys.path.insert(0, sys.argv[1])
import numpy as np, m, rep
from PIL import Image
OUT = sys.argv[1]
M = m.load('r_after_init')
src = np.frombuffer(rep.SEG[0x16ca3:0x16ca3+2048], np.uint8)
img = np.zeros((64, 64), np.uint8)
img[:, 0::2] = (src >> 4).reshape(64, 32) * 17
img[:, 1::2] = (src & 15).reshape(64, 32) * 17
Image.fromarray(img).resize((256, 256), Image.NEAREST).save(OUT + '/tex_src64x64.png')
pal = np.frombuffer(rep.SEG[0x169a3:0x169a3+768], np.uint8).reshape(256, 3).astype(int) * 255 // 63
sb = m.d32(M, 0x1eded)
S = np.array(M[sb:sb+65*256]).reshape(65, 256)
Image.fromarray(pal[S].astype(np.uint8)).resize((512, 260), Image.NEAREST).save(OUT + '/shade_table_defpal.png')
Mu = np.array(M[sb+0x10000:sb+0x10000+65*256]).reshape(65, 256)
Image.fromarray(Mu).resize((512, 260), Image.NEAREST).save(OUT + '/mul_table.png')
paper = np.array(M[m.d32(M, 0x1edf5):m.d32(M, 0x1edf5)+64000]).reshape(200, 320)
shade_paper = S[paper, 0x3e]
Image.fromarray(pal[shade_paper].astype(np.uint8)).save(OUT + '/paper_as_shown.png')
print(sorted(set(shade_paper.ravel().tolist())))
