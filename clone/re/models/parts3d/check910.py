"""Check the model of parts 9 and 10 against run_clone.py --p3d dumps: state recurrences, the metaball grid, the
cubes (vertices, faces, vertex shades, texture coordinates) and the tile band.

usage: python3 check910.py P.pkl P.frames P.flips
"""
import os, sys, pickle, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
import numpy as np
import model
import rep
from sheet import open_frames, to_rgb, read_flips

f32 = np.float32
recs = pickle.load(open(sys.argv[1], 'rb'))
raw, w, h, rec, hdr, n, _ = open_frames(sys.argv[2])
flips, flip0 = read_flips(sys.argv[3])
T = [struct.unpack('<i', rep.rd(0x428164 + 4 * i, 4))[0] for i in range(192)] + [0] * 64
FACES = [[struct.unpack('<i', rep.rd(0x428020 + 16 * k + 4 * j, 4))[0] for j in range(4)] for k in range(12)]
CORN = [[rep.rdf(0x4280e0 + 12 * k + 4 * j) for j in range(3)] for k in range(8)]
part = recs[0]['part']
stats = dict(state=0, grid=0, gridrel=0, ncube_bad=0, vert=0, face_bad=0, shade=0, uv=0, frames=0, tiles=[0, 0])
for r in recs:
    if 'after' not in r: continue
    stats['frames'] += 1
    b, a, dt = r['before'], r['after'], r['dt']
    if part == 9:
        k = r['ints'][0x428140]
        A = [f32(b[0xf73574]), f32(b[0xf73578]), f32(b[0xf7357c])]
        A[k] = f32(A[k] + f32(dt * 0.003 * (k + 1)))
        fade = f32(b[0x428010])
        if fade < 1 and r['ints'][0x428014] != 0:
            fade = f32(min(1.0, fade + dt * 0.005))
        ph = [f32(b[0xf73588] + dt * 0.02), f32(b[0xf7358c] + dt * 0.03), f32(b[0xf73590] + dt * 0.04)]
        E = f32(b[0xf735a0] + dt * 0.2)
        got = [a[0xf73574], a[0xf73578], a[0xf7357c], a[0x428010], a[0xf73588], a[0xf7358c], a[0xf73590], a[0xf735a0]]
        want = A + [fade] + ph + [E]
        G = model.grid9(E, fade)
        Gd = np.frombuffer(r['grid'], '<f4').reshape(8, 8, 8)
        cubes = model.cubes9(Gd)
        uvs = 0.003
    else:
        ang = f32(b[0xf73920] + dt * 0.002)
        ph = [f32(b[0xf73588] + dt * 0.02), f32(b[0xf7358c] + dt * 0.03), f32(b[0xf73590] + dt * 0.04)]
        E = f32(b[0xf73924] + dt * 0.3)
        got = [a[0xf73920], a[0xf73588], a[0xf7358c], a[0xf73590], a[0xf73924]]
        want = [ang] + ph + [E]
        g = model.grid10(E)
        gd = np.frombuffer(r['grid'], '<f4')
        G, Gd = g[:209], gd[:209]
        # values after the array (flat index >= 209): the program's variables at 0xf73904 + 4 (k - 216)
        extra = {216 + i: v for i, v in enumerate(np.frombuffer(r['grid'][216 * 4:], '<f4'))}
        cubes = model.cubes10(gd, lambda k: extra.get(k, 0.0) if k >= 216 else gd[k])
        uvs = 0.009
    stats['state'] = max(stats['state'], max(abs(float(x) - float(y)) / max(1, abs(float(y))) for x, y in zip(want, got)))
    stats['grid'] = max(stats['grid'], float(np.abs(G - Gd).max()))
    V, F, SH, UV = model.build_cubes(cubes, ph, uvs, FACES, CORN)
    src = np.frombuffer(r['src'], '<f4').reshape(-1, 13)[:, :3]
    if len(V) != r['nv'] or len(F) != r['nf']:
        stats['ncube_bad'] += 1
        if stats['ncube_bad'] < 4:
            print('flip %d: model %d vertices %d faces, dump %d %d' % (r['flip'], len(V), len(F), r['nv'], r['nf']))
        continue
    if len(V):
        stats['vert'] = max(stats['vert'], float(np.abs(V - src).max()))
        fd = np.frombuffer(r['faces'], '<u4').reshape(-1, 18)
        fi = (fd[:, 1:4].astype(np.int64) - r['dstbase']) // 0x34
        stats['face_bad'] += int((fi != F).any(1).sum())
        sh = fd[:, 15:18].view('<i4')
        stats['shade'] = max(stats['shade'], int(np.abs(sh - SH).max()))
        uv = np.frombuffer(r['faces'], '<f4').reshape(-1, 18)[:, 7:13]
        stats['uv'] = max(stats['uv'], float(np.abs(uv - UV).max()))
        assert (fd[:, 4] == 0).all()
    i = r['flip'] - 1 - flip0
    if 0 <= i < n:
        img = to_rgb(raw, i, w, h, rec, hdr).astype(np.int64)
        mt, wr = model.tiles(T, part)
        for y0 in range(4, 196, 24):
            for x0 in range(0, 320, 40):
                reg = img[y0:y0 + 24, x0:x0 + 40].reshape(-1, 3)
                cols, cnt = np.unique(reg, axis=0, return_counts=True)
                j = cnt.argmax()
                if cnt[j] > 240 and not (cols[j] == 255).all():
                    stats['tiles'][0] += 1
                    stats['tiles'][1] += int(not (cols[j] == mt[y0, x0]).all())
print('part %d, %d frames: max relative state error %.3g; max grid error %.3g; frames with other cube counts %d; '
      'max vertex error %.3g; faces with other vertices %d; max shade error %d (16.16); max uv error %.3g; '
      'tiles checked %d, wrong %d' % (part, stats['frames'], stats['state'], stats['grid'], stats['ncube_bad'],
                                      stats['vert'], stats['face_bad'], stats['shade'], stats['uv'], *stats['tiles']))
