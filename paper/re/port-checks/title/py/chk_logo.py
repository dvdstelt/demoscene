import sys, pickle
from tmodel import *
logo, lb = model.logo_buffer(G)
buf, base = model.b1996(G)
model.blur1996(buf, base, 150)
states = [buf[base:base+64000].copy()]
for k in range(120):
    model.f47(buf, base, 200, 320); states.append(buf[base:base+64000].copy())
pickle.dump(states, open('b1996_states.pkl', 'wb'))
def i32(v):
    v &= 0xffffffff
    return v - (1 << 32) if v & 0x80000000 else v
Z, K = 0xa000, 0xf230
Zs = [Z]
for n in range(900):
    Z = (Z * K) >> 16
    if Z < 0x2000: K = (((K - 0xffce) * 240) >> 8) + 0xffce
    Zs.append(Z)
pickle.dump(Zs, open('zs.pkl', 'wb'))
print('Z<0x500 at n', next(i for i, z in enumerate(Zs) if z <= 0x500))
for t in map(int, sys.argv[1:]):
    idx, _ = cap(t)
    found = []
    for n in range(t - 11380, t - 11373):
        if n < 0: continue
        s = Zs[n] >> 4
        for b in range(max(0, (t - 11377)//8 - 3), min(120, (t - 11377)//8 + 3)):
            _, vga = model.logo_frame(logo, lb, states[b].astype(np.int64), s)
            if np.array_equal(vga.reshape(200, 320).astype(np.uint8), idx): found.append((t - n, b))
    print(t, ticks[t][4], 'n = t - X, blur', found)
