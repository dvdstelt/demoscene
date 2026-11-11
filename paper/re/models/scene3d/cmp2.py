import sys, pickle
sys.path.insert(0, '.')
import model
from model import *
# 1) handwriting: memA at 0x7906, memB at 0x790b
cap = pickle.load(open('d4500.cap0.pkl', 'rb'))
R = Renderer(cap['memA'])
M = R.M
nz = M.m[0x69b7:0x69b7 + 0x800]
print('noise table recompute == mem:', noise_table(R) == nz, '(phases already advanced?)')
cam = R.b1f(M.i32(0x5dec), M.i32(0x5de8))
campos = [sar(M.i32(0x5df0 + 4 * i), 10) for i in range(3)]
print('camera matrix at 0x1e468 == b1f:', cam == [M.i32(0x1e468 + 4 * i) for i in range(9)])
buf = bytearray(M.m[R.back:R.back + 64000])
handwriting(R, buf, nz, cam, campos)
real = cap['memB'][R.back:R.back + 64000]
print('handwriting diff', sum(1 for i in range(64000) if buf[i] != real[i]), 'changed by 73a6', sum(1 for i in range(64000) if real[i] != M.m[R.back + i]))
# 2) banner vertices vs memory (set by 0x3c3e before 0x603f)
bo = M.u32(0x5dd8); vp = M.u32(bo + 0x78)
vs = banner_vertices(nz)
mv = [(M.i32(vp + 0x24 * i), M.i32(vp + 0x24 * i + 4), M.i32(vp + 0x24 * i + 8)) for i in range(187)]
print('banner vertices match:', vs == mv)
# normals
fp = M.u32(bo + 0x74)
bad = 0
for f in range(320):
    a = fp + 0x4c * f
    v0, v1, v2 = [M.u32(a + 4 * k) for k in range(3)]
    g = lambda q: [M.i32(q), M.i32(q + 4), M.i32(q + 8)]
    A, B, C = g(v0), g(v1), g(v2)
    e1 = [B[i] - A[i] for i in range(3)]; e2 = [B[i] - C[i] for i in range(3)]
    nx = e1[1] * e2[2] - e2[1] * e1[2]; ny = e1[2] * e2[0] - e2[2] * e1[0]; nz_ = e1[0] * e2[1] - e2[0] * e1[1]
    ln = int(round(__import__('math').sqrt(nx * nx + ny * ny + nz_ * nz_))) or 1
    tr = lambda q: int(q * 256 / ln) if False else (abs(q) * 256 // ln) * (1 if q >= 0 else -1)
    n = [tr(nx), tr(ny), tr(nz_)]
    if n != [M.i32(a + 0x24), M.i32(a + 0x28), M.i32(a + 0x2c)]: bad += 1
print('banner normals mismatches', bad)
# camera eval vs stored
print('camera eval', camera_eval(R), 'stored', [M.i32(0x5de8 + 4 * i) for i in range(5)], 'path param', M.u32(0x482b))
open('hw_model.raw','wb').write(bytes(buf)); open('hw_real.raw','wb').write(bytes(real))
d = bytearray(64000)
for i in range(64000):
    d[i] = 0x3f if buf[i] == real[i] else 0xef
open('hw_diff.raw','wb').write(bytes(d))
# list points
p = M.u32(0x483b); end = M.u32(0x4837)
pts = []
while p < end:
    pts.append((M.i32(p), M.i32(p+4))); p += 8
print(len(pts), pts[:30])
