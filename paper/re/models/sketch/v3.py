from mm import M
import numpy as np
s = M(1250)
tex = np.frombuffer(s.b, np.uint8, 64000, s.d(0x1edf5))
print('tex hist', np.unique(tex, return_counts=True))
T = np.frombuffer(s.b, np.uint8, 0x20000, s.d(0x1eded)).reshape(512,256)
# verify T against formula
ok = True
for ch in range(0x41):
    c = ch
    if ch > 0x20:
        c = ((ch-0x20)>>1) + ((ch-0x20)>>3)
    for cl in range(256):
        if cl < 0x40: v = max(0, cl + 2*c - 64)
        else: v = (cl & 0xe0) + max(0, (cl & 0x1f) + c - 32)
        if T[ch, cl] != (v & 0xff): ok = False; print('mismatch', ch, cl, T[ch,cl], v); break
        v2 = min(255, (cl*ch) >> 5)
        if T[256+ch, cl] != v2: ok=False; print('mismatch2', ch, cl); break
print('T formula ok', ok)
print('rows > 0x40 nonzero?', T[0x41:256].any())
# frame at 1250 from full.frames
FR = 64768
fr = np.memmap('/home/dennis/Downloads/acme_303/paper/re/full.frames', np.uint8, 'r')
f = fr[1250*FR:1250*FR+64000].reshape(200,320)
pred = T[tex, 0x3a].reshape(200,320)
eq = (f == pred)
print('frame1250 == T[tex][0x3a] fraction', eq.mean())
# grey table and jitter
g = np.frombuffer(s.b, np.uint8, 0x400, s.d(0x1ee09))
j = np.frombuffer(s.b, np.int8, 0x400, s.d(0x1ee0d))
print('grey', np.unique(g, return_counts=True))
print('jit', np.unique(j, return_counts=True))
