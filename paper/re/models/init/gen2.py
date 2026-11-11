import sys, struct, math
sys.path.insert(0, '/home/dennis/Downloads/acme_303/paper/re')
sys.path.insert(0, sys.argv[1])
import numpy as np
import rep, m
from PIL import Image
M = m.load('t120')
OUT = sys.argv[1]

def blur4(src_bytes, start, n, stride, before=None):
    """one pass of 0xf90: out[i] = (s[i-stride]+s[i+stride]+s[i-1]+s[i+1])>>2 for i in start..start+n"""
    s = src_bytes
    o = bytearray(n)
    for k in range(n):
        i = start + k
        o[k] = (s[i-stride] + s[i+stride] + s[i-1] + s[i+1]) >> 2
    return o

def f47(buf, off, rows, stride, pre=None):
    """0xf47 on buf (bytearray) at offset off: two passes, second pass with 640 zero bytes around"""
    n = rows * stride
    s1 = blur4(buf, off, n, stride)
    pad = bytearray(640) + s1 + bytearray(640)
    s2 = blur4(pad, 640, n, stride)
    buf[off:off+n] = s2

# --- 0x10d3: 256x256 texture
src = rep.SEG[0x16ca3:0x16ca3 + 2048]
T = np.zeros((256, 256), np.uint8)
for r in range(64):
    row = []
    for b in src[r*32:(r+1)*32]:
        hi, lo = b >> 4, b & 15
        row += [hi*16, hi*16, lo*16, lo*16]
    row = np.array(row, np.uint8)
    full = np.concatenate([row, row[::-1]])
    T[2*r] = full; T[2*r+1] = full
T[128:] = T[:128][::-1, ::-1]
Image.fromarray(T).save(OUT + '/tex256_unblurred.png')
# blur with wrap layout
cur = T.copy()
for it in range(3):
    lay = np.zeros((512, 260), np.uint8)
    lay[:256, :256] = cur; lay[256:, :256] = cur
    lay[:, 256:] = lay[:, :4]
    buf = bytearray(lay.tobytes()) + bytearray(4)  # padding beyond
    # memory before buffer: unknown; pass reads s[i-stride] for i>=0 -> index negative: use a prefix
    pre = 600
    b2 = bytearray(pre) + buf + bytearray(600)
    f47(b2, pre, 512, 260)
    lay2 = np.frombuffer(bytes(b2[pre:pre+512*260]), np.uint8).reshape(512, 260)
    flat = lay2.reshape(-1)
    cur = np.array([flat[0x20a + y*260: 0x20a + y*260 + 256] for y in range(256)], np.uint8)
base = m.d32(M, 0x1eddd)
snapT = np.array(M[base:base+65536]).reshape(256, 256)
print('tex256 match', np.array_equal(cur, snapT), np.sum(cur != snapT))
Image.fromarray(snapT).save(OUT + '/tex256.png')
print('tex256 range', snapT.min(), snapT.max())
# what is at +0x10000 .. 0x20000
nxt = np.array(M[base+65536:base+131072]).reshape(256, 256)
print('tex256+64K nonzero', np.count_nonzero(nxt))

# --- 0x144b tables
sq = [m.i32(M, 0x21455 + 4*k) for k in range(512)]
print('square table intact?', all(sq[k] == (k-256)**2 for k in range(512)))
sb = m.d32(M, 0x1eded)
S = np.array(M[sb:sb+0x20000]).reshape(512, 256)
ok1 = True; ok2 = True
for ch in range(65):
    k = ch if ch <= 32 else ((ch - 32) >> 1) + ((ch - 32) >> 3)
    for cl in range(256):
        mul = min(255, (cl*ch) >> 5)
        if S[256 + ch, cl] != mul: ok2 = False
        if cl < 64: v = max(0, cl + 2*k - 64)
        else: v = max(0, (cl & 31) + k - 32) + (cl & 0xe0)
        if S[ch, cl] != v: ok1 = False
print('shade table ok', ok1, 'mul table ok', ok2)
print('shade rows 65..255 nonzero', np.count_nonzero(S[65:256]), 'mul rows beyond', np.count_nonzero(S[256+65:]))

# --- 0xc80 lightmap
lb = m.d32(M, 0x1edd1)
L = np.array(M[lb:lb+0x20000]).reshape(256, 512)
mine = np.zeros((256, 512), np.uint8)
for j in range(256):
    y = -128 + j
    row = (256 - j) & 255
    for i in range(512):
        x = -256 + i
        d = int(np.float64(math.sqrt(math.sqrt(x*x + y*y))) * 32.0)
        d = int(round(math.sqrt(math.sqrt(x*x+y*y))*32.0))
        v = 300 - d
        v = max(0, min(255, v))
        if v > 0xc0: a = (v - 0x80) & 0xff
        else: a = (v * 0x5555) >> 16
        a = (a << 1) & 0xff
        a = (a - (a >> 3)) & 0xff
        t = int(snapT[row, (512 - i) & 255]) >> 1
        a = min(255, a + t)
        mine[j, i] = a
print('lightmap match', np.array_equal(mine, L), np.sum(mine != L))
Image.fromarray(L).save(OUT + '/lightmap512x256.png')
