import numpy as np, sys
sys.path.insert(0, '../re-planes/py')
from flockmodel import Rng
from snap import M
m = M(7000)
base = np.frombuffer(m.b(0x2b0000, 65536), np.uint8)
tex = np.load('../re-planes/py/tex_7000.npy').reshape(-1)
r = Rng(); r.skip(163185)
bits = np.zeros(65536, np.uint8)
for k in range(16384):
    x = r()
    for b in range(4): bits[4 * k + b] = (x >> (8 * b + 1)) & 1
mine = 0x8b + ((base >> 5) & 7) + 2 * bits
print('match', (mine == tex).mean(), 'calls', r.n)
