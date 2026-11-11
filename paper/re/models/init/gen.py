import sys, struct, math
sys.path.insert(0, '/home/dennis/Downloads/acme_303/paper/re')
sys.path.insert(0, sys.argv[1])
import numpy as np
import rep, m
M = m.load('t120')

class Rand:
    def __init__(self):
        self.t = [rep.rd32(0x9a0 + 4*i) for i in range(64)]
        self.i, self.j = rep.SEG[0xaa0], rep.SEG[0xaa1]
    def __call__(self):
        v = (self.t[self.i] + self.t[self.j]) & 0xffffffff
        self.t[self.j] = v
        self.i = (self.i + 1) & 63; self.j = (self.j + 1) & 63
        return v
R = Rand()
for _ in range(72): R()
# shades
sh = []
for _ in range(1024):
    a = R() & 0x1f
    if a < 5: a = (a >> 2) + 0x1e
    else: a = (a >> 3) + 0x1a
    if a > 0x20: a = 0x1f
    sh.append(a)
base = m.d32(M, 0x1ee05)
print('shades match', bytes(sh) == bytes(M[base:base+1024]), np.bincount(sh)[24:])
jit = []
for _ in range(1024):
    s = 0
    for k in range(120):
        s += (R() & 0xf) - 7
    s >>= 6
    jit.append((s - 1) & 0xff)
print('jitter match', bytes(jit) == bytes(M[base+1024:base+2048]), np.bincount(np.array(jit, dtype=np.uint8).view(np.int8).astype(int) + 8))
# paper
pb = m.d32(M, 0x1edf5)
buf = bytearray()
for _ in range(0x3ed1):
    buf += struct.pack('<I', R() & 0x0f0f0f0f)
buf += bytes(0x10200 - len(buf))  # beyond: whatever (allocated memory: zero in emulator)
out = bytearray(buf)
for i in range(64000):
    d = (out[i] - out[i + 0x141]) & 0xff
    d = (d - 256 if d > 127 else d) >> 3
    v = (d + 0x1e) & 0xff
    if v > 0x1f: v = 0x1f
    out[i] = v
print('paper match', bytes(out[:64000]) == bytes(M[pb:pb+64000]), np.bincount(np.frombuffer(bytes(out[:64000]), np.uint8))[26:])
print('paper beyond 64000 match', bytes(out[64000:0x3ed1*4]) == bytes(M[pb+64000:pb+0x3ed1*4]))
# sine
st = [m.i32(M, 0x22479 + 4*k) for k in range(1024)]
x = np.float32(0.00306796); print('const', struct.unpack('<f', bytes(M[0x1e464:0x1e468]))[0], struct.unpack('<f', bytes(M[0x1e460:0x1e464]))[0])
c = struct.unpack('<f', bytes(M[0x1e464:0x1e468]))[0]
mine = [int(round(65536*math.sin(2*c*k))) for k in range(1024)]
diff = [k for k in range(1024) if mine[k] != st[k]]
print('sine diffs vs round(65536 sin(2c k))', len(diff), diff[:10], [(st[k], mine[k]) for k in diff[:5]])
ideal = [int(round(65536*math.sin(2*math.pi*k/1024))) for k in range(1024)]
diff2 = [k for k in range(1024) if ideal[k] != st[k]]
print('sine diffs vs ideal', len(diff2), max(abs(ideal[k]-st[k]) for k in range(1024)))
print('sin[0],[256],[512],[768]', st[0], st[256], st[512], st[768])
rec = [m.d32(M, 0x1ee19 + 4*k) for k in range(2047)]
print('recip ok', all(rec[k] == 65536 // (k+1) for k in range(2047)), m.d32(M, 0x1ee19 + 4*2047))
