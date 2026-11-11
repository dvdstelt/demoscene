"""Exports one render of the original engine to a case file for enginetest (see enginetest.cpp for the format).

The input is a render dump of the emulator (data generated from JIZZ.EXE: keep dumps and case files outside the
repository), for example, inside the toolbox from jizz/re:
    python3 models/engine/run_engine.py --load --snap snaps/t3285.snap --out $D/x --ticks 3712 \
        --dump-render $D --dump-max 3 --dump-tick0 3703
    python3 ../remake/tools/enginetest/export_case.py $D 0 $D/tube0.bin 3706
usage: python3 export_case.py DUMPDIR N OUT.bin [CAPTURE_TICK]
CAPTURE_TICK: a full.frames record showing this page (models/engine/match_capture.py), stored for reference.
The colour-table parameters E / S are recovered from the dumped tables (F is [0x1a478])."""
import pickle, struct, sys
import numpy as np

d, n, out = sys.argv[1], int(sys.argv[2]), sys.argv[3]
cap = int(sys.argv[4]) if len(sys.argv) > 4 else -1
pre = pickle.load(open('%s/r%04d.pre' % (d, n), 'rb'))
post = pickle.load(open('%s/r%04d.post' % (d, n), 'rb'))
low = pre['low']; eng = pre['eng']; elo = pre['eng_lo']; tex = pre['tex']
u32 = lambda a: struct.unpack_from('<I', low, a)[0]
f32 = lambda a: struct.unpack_from('<f', low, a)[0]
def eb(a, n):
    if 0x220000 <= a < 0x560000: return tex[a - 0x220000:a - 0x220000 + n]
    if elo <= a < elo + len(eng): return eng[a - elo:a - elo + n]
    raise KeyError(hex(a))
eu32 = lambda a: struct.unpack('<I', eb(a, 4))[0]

scene = u32(0x157a0); W, H = u32(0x18d64), u32(0x18d68)
off = pre['edi'] - pre['page']; wx, wy = (off % 640) // 2, off // 640
t = f32(0x159f7); focal = f32(0x154b4); wob = low[0x15c3f]
Finv = low[0x1a478]
TR = np.frombuffer(low[0x1a479:0x1a479 + 768 * 4], '<u4')
TG = np.frombuffer(low[0x1b079:0x1b079 + 768 * 4], '<u4')
i = np.arange(768)
best = None
for E in range(-255, 256):
    for S in (0x100, 0x1c8, 0x132):
        v = np.clip((i * S + E * 256) >> 8, 0, 255)
        v = ((255 - Finv) * v + v + (255 - v) * Finv) >> 8
        if np.array_equal(((v << 5) >> 8) << 11, TR) and np.array_equal(((v << 6) >> 8) << 5, TG):
            best = (E, S); break
    if best: break
assert best, 'colour table parameters not found'
E, S = best
OB = u32(0x18d50); VB = u32(0x18d4c); FB = u32(0x18d48)
nobj = u32(0x18d58); nv = u32(0x18d5c); nf = u32(0x18d60)
objs = [struct.unpack('<IIII', eb(OB + o * 0x50, 16)) + (eu32(OB + o * 0x50 + 0x4c),) for o in range(nobj)]
mat = struct.unpack('<13f', eb(OB + 0x10, 52))
# vertex / face counts: the metaball part's counters are not the totals (notes-parts3d-b 5.1): take the max extent
nv = max(o[2] + o[3] for o in objs); nf = max(o[0] + o[1] for o in objs)
with open(out, 'wb') as f:
    f.write(b'JZEC' + struct.pack('<I', 1))
    f.write(struct.pack('<6I', scene, W, H, wx, wy, wob))
    f.write(struct.pack('<2f', t, focal))
    f.write(struct.pack('<3i', E, S, Finv))
    f.write(struct.pack('<13f', *mat))
    f.write(struct.pack('<2i', pre['tick'], cap))
    f.write(pre['pagebytes']); f.write(post['pagebytes'])
    for s in range(1, 18):
        a = u32(0x654a + 4 * s)
        f.write(eb(a, 0x30000))
    f.write(struct.pack('<I', nobj))
    for o in objs: f.write(struct.pack('<5I', *o))
    f.write(struct.pack('<I', nv))
    for k in range(nv):
        a = VB + k * 0x40
        f.write(eb(a, 12) + eb(a + 0x20, 12) + eb(a + 0x2c, 8))
    f.write(struct.pack('<I', nf))
    for k in range(nf):
        a = FB + k * 0x1c
        idx = [(eu32(a + 4 * j) - VB) // 0x40 for j in range(3)]
        fc, ty, tx, en = eb(a + 0xc, 4)
        uv = struct.unpack('<6f', eb(eu32(a + 0x10), 24)) if fc & 0x80 else (0.0,) * 6
        f.write(struct.pack('<3I4B6f', *idx, fc, ty, tx, en, *uv))
print('%s: scene %d %dx%d at (%d,%d) t %.5f wobble %d E %d S %#x F %d objects %d vertices %d faces %d' %
      (out, scene, W, H, wx, wy, t, wob, E, S, Finv, nobj, nv, nf))
