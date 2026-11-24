import sys, struct, pickle
sys.path.insert(0, '../..')
import numpy as np
import unicorn
import rep
v = rep.View('code32')
img = v.img
def f32(a): return struct.unpack_from('<i', img, 0xec0 + a)[0]
snap = pickle.load(open('../../snaps/part_3554b.snap', 'rb'))
mem = snap['mem']
def s32(a): return struct.unpack_from('<i', mem, 0x8fc0 + a)[0]
for a in [0x2ff30,0x2ff34,0x2ff38,0x2ff3c,0x2ff40,0x2ff44,0x2ff48,0x2ff4c,0x2ff50,0x2ff54,0x2ff58,0x2ff5c,0x2f21c,0x2f220,0x2f224,0x2f2ac,0x2ff9c]:
    print(hex(a), f32(a), s32(a))
print('vertices file:', [f32(0x2ffa8 + 12*i + k) for i in range(20) for k in (0,4,8)])
print('vertices snap equal:', bytes(img[0xec0+0x2ffa8:0xec0+0x31aa8]) == bytes(mem[0x8fc0+0x2ffa8:0x8fc0+0x31aa8]))
xs = [f32(0x2ffa8 + 12*i) for i in range(256)]; ys = [f32(0x2ffa8 + 12*i+4) for i in range(256)]
print('x', xs[:32]); print('y', ys[:32])
e = [(img[0xec0+0x30ba8+8*i], img[0xec0+0x30ba8+8*i+4], img[0xec0+0x30ba8+8*i+1:0xec0+0x30ba8+8*i+4].hex(), img[0xec0+0x30ba8+8*i+5:0xec0+0x30ba8+8*i+8].hex()) for i in range(480)]
print('edges', e[:40], e[-10:])
print('rowtab', [f32(0x34da8+4*i) for i in range(10)], all(f32(0x34da8+4*i)==320*i for i in range(200)))
print('sin', [f32(0x24a3c+4*i) for i in range(0,4096,256)], 'n?', [f32(0x24a3c+4*i) for i in range(4090,4100)], [f32(0x24a3c+4*i) for i in range(5115,5125)])
m = np.frombuffer(img[0xec0+0x2f2b0:0xec0+0x2f2b0+3200], np.uint8).reshape(200,16)
print(m[:20])
print('history', [f32(0x2f228+4*i) for i in range(34)])
pal = np.frombuffer(img[0xec0+0x347a8:0xec0+0x347a8+1536], np.uint8).reshape(2,256,3)
print(pal[0][:20].tolist(), pal[0].max(), pal[1].max()); print(pal[1][:20].tolist())
print('snap pal eq', bytes(img[0xec0+0x347a8:0xec0+0x347a8+1536]) == bytes(mem[0x8fc0+0x347a8:0x8fc0+0x347a8+1536]))
