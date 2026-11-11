import sys, pickle
sys.path.insert(0, '.')
from model import *
cap = pickle.load(open(sys.argv[1], 'rb'))
R = Renderer(cap['memA']); M = R.M
M.w32(0x1ea95, 0x113)
M.m[0x1ea94] = 0
col = R.scene(0)
M.m[0x1ea94] = 2
sk = R.scene(2)
comp = composite72c4(R, col, sk)
out = R.pencil(comp)
real = cap['memB'][R.back:R.back + 64000]
d = [i for i in range(64000) if out[i] != real[i]]
print('diff', len(d), [(i % 320, i // 320, out[i], real[i]) for i in d[:10]])
open('t_model.raw', 'wb').write(bytes(out)); open('t_real.raw', 'wb').write(bytes(real))
dd = bytearray(0x3f if out[i] == real[i] else 0xef for i in range(64000)); open('t_diff.raw', 'wb').write(bytes(dd))
