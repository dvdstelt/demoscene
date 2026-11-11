import sys, pickle, struct
import unicorn
import numpy as np
from PIL import Image
snap = pickle.load(open(sys.argv[1], 'rb'))
mem = snap['mem']
out = sys.argv[2]
def r32(a): return struct.unpack_from('<I', mem, a)[0]
names = {k: r32(k) for k in [0x1eddd,0x1eded,0x1edf5,0x1ede9,0x1edcd,0x1edd5,0x1edd9,0x1ede1,0x1ede5,0x1edfd,0x1edf9,0x2245d,0x14dd,0x1edb5,0x1edbd,0x1edb9,0x1ea8b,0x1368,0x1364,0x1edad,0x1edc1,0x1e5ef, 8]}
for k,v in names.items(): print(hex(k), hex(v))
print('1505', mem[0x1505], '2efc', mem[0x2efc], '2f02', mem[0x2f02], 'eb8', hex(r32(0xeb8)), '22459', mem[0x22459])
np.save(out+'_mem_small.npy', np.frombuffer(mem[:0x400000], dtype=np.uint8))
tex = np.frombuffer(mem, np.uint8, 65536, names[0x1eddd]).reshape(256,256)
Image.fromarray(tex).save(out+'_tex0.png')
tex1 = np.frombuffer(mem, np.uint8, 65536, names[0x1eddd]+0x10000).reshape(256,256)
Image.fromarray(tex1).save(out+'_tex1.png')
t2 = np.frombuffer(mem, np.uint8, 65536*2, names[0x1eddd]+0x20000).reshape(512,256)
Image.fromarray(t2).save(out+'_shade.png')
b = np.frombuffer(mem, np.uint8, 64000, names[0x1ede1]).reshape(200,320)
Image.fromarray(b).save(out+'_1996.png')
l = np.frombuffer(mem, np.uint8, 0x280*40, names[0x1ede5]).reshape(40,640)
Image.fromarray(l).save(out+'_logo.png')
f = np.frombuffer(mem, np.uint8, 0xe800, names[0x1ede9])
glyphs = []
for g in range(29):
    w = f[g*2048]
    gl = f[g*2048+1:g*2048+1+1920].reshape(32,60)
    glyphs.append((w, gl))
print('widths', [int(w) for w,_ in glyphs])
sheet = np.zeros((32*3+6, 62*10), np.uint8)
for g,(w,gl) in enumerate(glyphs):
    y, x = (g//10)*34, (g%10)*62
    sheet[y:y+32, x:x+60] = gl
Image.fromarray(sheet).save(out+'_font.png')
