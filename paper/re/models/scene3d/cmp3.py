import sys, pickle
sys.path.insert(0, '.')
from model import *
cap = pickle.load(open('d4500.cap0.pkl', 'rb'))
R = Renderer(cap['memA']); M = R.M
nz = M.m[0x69b7:0x69b7 + 0x800]
cam = [M.i32(0x1e468 + 4 * i) for i in range(9)]
campos = [M.i32(0x1ea9d), M.i32(0x1eaa1), M.i32(0x1eaa5)]
print('campos', campos, [sar(M.i32(0x5df0 + 4 * i), 10) for i in range(3)])
calls = []
orig = R.line18b7
R.line18b7 = lambda buf, a, b, c, d: (calls.append((a, b, c, d)), orig(buf, a, b, c, d))
buf = bytearray(M.m[R.back:R.back + 64000])
handwriting(R, buf, nz, cam, campos)
real = [tuple(map(int, l.split()[2:6])) for l in open('e4500.regs') if l.split()[1] == '7408' and l.split()[0] == '4500']
print(len(calls), len(real))
for i, (a, b) in enumerate(zip(calls, real)):
    if a != b:
        print('first diff at', i, a, b); break
realbuf = cap['memB'][R.back:R.back + 64000]
print('handwriting pixel diff', sum(1 for i in range(64000) if buf[i] != realbuf[i]))
# camera eval check: stored camera = eval at previous path param
M2 = Renderer(cap['memA']).M
for back in range(0, 4):
    p = M.u32(0x482b)
    M2.w32(0x482b, p - back)
    R2 = Renderer(bytes(M2.m)); 
    print('back', back, camera_eval(R2), [M.i32(0x5de8 + 4 * i) for i in range(5)], '466b', M.u32(0x466b), M.u32(0x466b) >> 7)
