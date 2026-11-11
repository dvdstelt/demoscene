import sys, struct
sys.path.insert(0, '/home/dennis/Downloads/acme_303/paper/re'); sys.path.insert(0, sys.argv[1])
import rep, m
import numpy as np
S = rep.SEG
M = m.load('r_after_init')
# font decode
p = 0x18ed7
font = bytearray(29 * 2048)
for g in range(29):
    o = g * 2048
    font[o] = S[p]; p += 1
    for r in range(32):
        q = o + 1 + r * 60
        while True:
            b = S[p]; p += 1
            if b == 0: break
            if b == 0xff:
                skip, cnt = S[p], S[p+1]; p += 2
            else:
                skip, cnt = b & 15, b >> 4
            q += skip
            for _ in range(cnt):
                font[q] = 0xff; q += 1
fb = m.d32(M, 0x1ede9)
print('font match', bytes(font) == bytes(M[fb:fb+len(font)]), 'data end', hex(p))
# 1996 logo: 0xedc into 320x200 (+guard rows), then 75 blur passes (0xf47 = 2 x 4-neighbour)
buf = bytearray(320 * 202)   # row 0 = guard, rows 1..200 image, row 201 guard
edi = 320 + 0xc82
def edc(ch, edi):
    g = (ch - 0x61) * 2048
    w = font[g]
    src = g + 1
    for r in range(25):
        e = edi
        for c in range(w):
            pix = font[src + c]
            for k in range(7):
                for d in range(4):
                    buf[e + k*320 + d] = pix if d < 3 else 0
            e += 3
        edi += 0x8c0
        src += 60
    # restore: edi after loop: start + 25*0x8c0; then return start + 3w+2
    return w
x = 0xc82
for ch in (0x7b, 0x7c, 0x7c, 0x7d):
    w = edc(ch, x)
    x += 3*w + 2
def blur(buf):
    n = 64000
    s = buf
    o1 = bytearray(n)
    for i in range(n):
        j = 320 + i
        o1[i] = (s[j-320] + s[j+320] + s[j-1] + s[j+1]) >> 2
    pad = bytearray(640) + o1 + bytearray(640)
    for i in range(n):
        j = 640 + i
        s[320 + i] = (pad[j-320] + pad[j+320] + pad[j-1] + pad[j+1]) >> 2
import time
a = np.frombuffer(bytes(buf), np.uint8).reshape(202, 320).astype(np.int32)
def blur_np(img):
    # img: 202x320 with guard rows; linear-memory neighbours
    flat = img.reshape(-1)
    n = 64000
    j = np.arange(320, 320 + n)
    o1 = (flat[j-320] + flat[j+320] + flat[j-1] + flat[j+1]) >> 2
    pad = np.concatenate([np.zeros(640, np.int32), o1, np.zeros(640, np.int32)])
    j2 = np.arange(640, 640 + n)
    o2 = (pad[j2-320] + pad[j2+320] + pad[j2-1] + pad[j2+1]) >> 2
    out = flat.copy(); out[320:320+n] = o2
    return out.reshape(202, 320)
lb = m.d32(M, 0x1ede1)
mem = np.array(M[lb:lb+64000]).reshape(200, 320)
cur = a.copy()
for k in range(75):
    cur = blur_np(cur)
print('logo glow (75 passes) match', np.array_equal(cur[1:201].astype(np.uint8), mem), np.sum(cur[1:201] != mem))
# psychic link text: 640 x 42 alloc, pointer at +640
tb = bytearray(640 * 42)
base = 640
e = base + 0xfbb
for ch in b'psychic link':
    if ch < 0x61:
        e += 10; continue
    g = (ch - 0x61) * 2048; w = font[g]
    for r in range(32):
        for c in range(w):
            v = tb[e + r*640 + c] + font[g + 1 + r*60 + c]
            tb[e + r*640 + c] = 0xff if v > 0xff else v
    e += w + 1
def notrect(x, y, w, h):
    o = base + y*640 + x
    for r in range(h):
        for c in range(w):
            tb[o + r*640 + c] ^= 0xff
notrect(0xb0, 1, 0x120, 0x26)
notrect(0xb4, 4, 0xc4, 0x20)
arr = np.frombuffer(bytes(tb), np.uint8).astype(np.int32)
n = 40 * 640
ext = np.concatenate([arr, np.zeros(2000, np.int32)])
j = np.arange(base, base + n)
o1 = (ext[j-640] + ext[j+640] + ext[j-1] + ext[j+1]) >> 2
pad = np.concatenate([np.zeros(640, np.int32), o1, np.zeros(640, np.int32)])
j2 = np.arange(640, 640 + n)
o2 = (pad[j2-640] + pad[j2+640] + pad[j2-1] + pad[j2+1]) >> 2
arr2 = arr.copy(); arr2[base:base+n] = o2
tb2 = arr2.astype(np.uint8)
pb = m.d32(M, 0x1ede5)
mem = np.array(M[pb - 640: pb - 640 + 640*42])
print('psychic link match', np.array_equal(tb2, mem), np.sum(tb2 != mem))
