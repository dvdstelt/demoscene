"""Find the axis / sign conventions of the rotation helpers from the matrices dumped by run_clone.py --p3d."""
import pickle, sys
import numpy as np
recs = pickle.load(open(sys.argv[1], 'rb'))
def Rx(a):
    c, s = np.cos(a), np.sin(a); return np.array([[1, 0, 0], [0, c, -s], [0, s, c]])
def Ry(a):
    c, s = np.cos(a), np.sin(a); return np.array([[c, 0, s], [0, 1, 0], [-s, 0, c]])
def Rz(a):
    c, s = np.cos(a), np.sin(a); return np.array([[c, -s, 0], [s, c, 0], [0, 0, 1]])
import itertools
r = [x for x in recs if 'mat' in x][5]
M = np.frombuffer(r['mat'], '<f4')[:12].reshape(3, 4).astype(float)
part = r['part']
if part == 7: a = r['after'][0xf61400]; angs = (a, a * 1.1, a * 0.8); sy = 4 / 3
elif part == 8: a = r['after'][0xf72d6c]; angs = (a, a * 1.1, a * 0.8); sy = 1.4814814
elif part == 9:
    A0, A1, A2 = r['after'][0xf73574], r['after'][0xf73578], r['after'][0xf7357c]
    angs = (A0 + A1 - A2, (A0 - A1 + A2) * 1.1, (A1 - A0 + A2) * 0.8); sy = 4 / 3
else: a = r['after'][0xf73920]; angs = (a, a * 1.1, a * 0.8); sy = 4 / 3
print('part', part, 'angles', angs, '\nM =\n', M)
S = np.diag([1, sy, 1])
best = []
for sx, sy_, sz in itertools.product((1, -1), repeat=3):
    R = S @ Rz(sz * angs[2]) @ Ry(sy_ * angs[1]) @ Rx(sx * angs[0])
    best.append((np.abs(R - M[:, :3]).max(), sx, sy_, sz))
best.sort()
print('best S Rz(+-c) Ry(+-b) Rx(+-a):', best[:2])
