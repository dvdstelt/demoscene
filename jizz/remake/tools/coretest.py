"""coretest.py: compares the output of tools/coretest (the core library, src/jizz) with the Python models of
jizz/re/models (core, parts3d-a) and the precalc tables of the t430 snapshot. Run from jizz/re inside the toolbox:

    coretest EXE rng > OUT.txt;       python3 ../remake/tools/coretest.py rng OUT.txt
    coretest EXE glow DIR;            python3 ../remake/tools/coretest.py glow DIR
    coretest EXE tables DIR;          python3 ../remake/tools/coretest.py tables DIR      (needs snaps/t430.snap)
"""
import os, sys, struct
import numpy as np
MODELS = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 're', 'models')
sys.path.insert(0, os.path.join(MODELS, 'core'))
sys.path.insert(0, os.path.join(MODELS, 'parts3d-a'))
from rng import Rng
W, H = 320, 200
SEED = (0x1234abce, 0x03049291, 0x67fcd9a8)   # 0x12e72's immediates (notes-core 4)


def check_rng(path):
    lines = open(path).read().split('\n')
    g = Rng()
    bad = sum(int(lines[i], 16) != g.next() for i in range(1000))
    print('first 1000 outputs from (0,0,0):', bad, 'differ')
    g = Rng(); [g.next() for _ in range(80)]
    st = {l.split()[0]: tuple(int(x, 16) for x in l.split()[1:4]) for l in lines[1000:1004] if l}
    print('after precalc', st['precalc'] == g.state())
    print('seed', st['seed'] == SEED)
    g = Rng(*SEED); [g.next() for _ in range(131070)]
    print('textures', st['textures'] == g.state(), 'snapshot t430:', st['textures'] == (0x44273c73, 0x19e7ee6a, 0xd3a3c270))
    [g.next() for _ in range(620)]
    print('ribbons', st['ribbons'] == g.state(), 'snapshot t2515:', st['ribbons'] == (0x296a9e46, 0x815e2d10, 0x768000e0))
    g = Rng(); r = [g.next() for _ in range(80)]
    l = lines[1004].split()
    print('precalcRandoms', int(l[1], 16) == r[0], int(l[3], 16) == r[79])


def check_glow(D):
    import glow
    import rects
    # 1. quads (0x18228) of the tool vs the parts3d-a model
    q = [list(map(float, l.split())) for l in open(D + '/quads.txt')]
    want = rects.corners(rects.CONFIGS['precalc 0x181f4'])
    print('0x18228 quads equal to the model:', q == [list(map(float, w)) for w in want])
    qb = b''.join(struct.pack('<8i', *w) for w in want)

    def ror(x, n): return ((x >> n) | (x << (32 - n))) & 0xffffffff
    def segs_model(rng, first, last, mask):
        out = []
        for n in range(first, last + 1):
            P = struct.unpack_from('<8i', qb, 32 * n)
            for k in range(4):
                r = rng.next(); k1 = (k + 1) & 3
                x0 = P[2*k] + (r & mask); r = ror(r, 4); y0 = P[2*k+1] + (r & mask); r = ror(r, 4)
                x1 = P[2*k1] + (r & mask); r = ror(r, 4); y1 = P[2*k1+1] + (r & mask)
                if 0 <= x0 < W and 0 <= x1 < W and 0 <= y0 < H and 0 <= y1 < H and (x0, y0) != (x1, y1):
                    out.append((x0, y0, x1, y1))
        return out

    def read_segs(name):
        out = []
        for l in open(D + '/' + name):
            v = l.split()
            out.append([tuple(map(float, s.split(','))) for s in v[1:]])
        return out

    # 2. jittered segments (0x183a9 + RNG + rejection), exact
    rng = Rng(*SEED); [rng.next() for _ in range(131070)]
    got = read_segs('segs_rects.txt')
    bad = 0
    sched = []
    for k in range(60):
        first, last = rects.select_p1((k // 4) & 15)
        sched.append((first, last))
        m = segs_model(rng, first, last, 7)
        bad += m != [tuple(map(int, s)) for s in got[k]]
    print('0x183a9 jittered segments, 60 steps:', bad, 'steps differ;', sum(len(g) for g in got), 'segments')

    # 3. buffers
    def int_model(steps, draw):
        b = np.zeros(0x20000, np.uint8)
        out = []
        for k in range(steps):
            bb = bytearray(b.tobytes()); draw(k, bb); b = np.frombuffer(bytes(bb), np.uint8).copy()
            glow.shr3(b); glow.blur_np(b, 0, 0); glow.trail(b, 0, 0x10000, 1)
            out.append(b[0x10000:0x10000 + 64000].reshape(H, W).astype(np.float64))
        return out

    w = np.exp(-0.5 * (np.arange(-1, 3) - 0.5) ** 2 / 0.25); w /= w.sum()
    def gauss(a):
        p = np.pad(a, ((0, 0), (1, 2)), mode='edge'); a = sum(w[i] * p[:, i:i + W] for i in range(4))
        p = np.pad(a, ((1, 2), (0, 0)), mode='edge'); return sum(w[i] * p[i:i + H, :] for i in range(4))

    def float_model(steps, draw):
        B = np.zeros((H, W)); T = np.zeros((H, W)); out = []
        for k in range(steps):
            m = bytearray(0x10000 + 4 * W); draw(k, m)
            mask = np.frombuffer(bytes(m), np.uint8)[:64000].reshape(H, W) != 0
            X = np.where(mask, 249.0, B); B = gauss(X / 8); T = T + (B - T) / 2
            out.append(T.copy())
        return out

    def draw_rects(k, buf, rng=[None]):
        if k == 0: rng[0] = Rng(*SEED); [rng[0].next() for _ in range(131070)]
        first, last = sched[k]
        glow.quads(buf, qb, first, last, 7, rng[0])

    def draw_keys(k, buf):
        glow.quads(buf, qb, 0, 0, 0, Rng()); glow.quads(buf, qb, 7, 7, 0, Rng())

    for name, steps, draw in (('glow_keys.f32', 40, draw_keys), ('glow_rects.f32', 60, draw_rects)):
        gpu = np.fromfile(D + '/' + name, np.float32).reshape(steps, H, W).astype(np.float64)
        im = int_model(steps, draw)
        fm = float_model(steps, draw)
        print(f'--- {name}: per step: mean of the trail (GPU / float model / integer model), max |GPU - float model|, mean |GPU - integer|')
        if name == 'glow_rects.f32':
            print('    (slanted lines: the models draw the DDA staircase, the GPU anti-aliased strokes, so the maxima differ)')
        for k in list(range(0, 6)) + list(range(9, steps, 10)):
            print(f'  step {k:2d}: mean {gpu[k].mean():6.3f} / {fm[k].mean():6.3f} / {im[k].mean():6.3f}   '
                  f'max|gpu-float| {np.abs(gpu[k] - fm[k]).max():6.3f}   mean|gpu-int| {np.abs(gpu[k] - im[k]).mean():6.3f}  '
                  f'max|gpu-int| {np.abs(gpu[k] - im[k]).max():6.2f}')
        a = gpu[-1].ravel(); b = im[-1].ravel()
        print(f'  last step: correlation GPU / integer {np.corrcoef(a, b)[0, 1]:.4f}; pixels > 0.5: {np.count_nonzero(a > .5)} / {np.count_nonzero(b > .5)}')


def check_tables(D):
    import importlib.util
    spec = importlib.util.spec_from_file_location('core_snap', os.path.join(MODELS, 'core', 'snap.py'))
    snap = importlib.util.module_from_spec(spec); spec.loader.exec_module(snap)
    load, rd32, rdb = snap.load, snap.rd32, snap.rdb
    s = load(430)
    P = np.frombuffer(rdb(s, rd32(s, 0x16f4b), 340 * 600 * 4), np.uint32).reshape(340, 600)
    S = np.frombuffer(rdb(s, rd32(s, 0x654a), 0x10000), np.uint8).reshape(256, 256)
    g = np.fromfile(D + '/polar.f32', np.float32).reshape(340, 600, 4).astype(np.float64)
    u = (np.rint(g[..., 0]).astype(np.int64) & 255); v = (np.rint(g[..., 1]).astype(np.int64) & 255)
    du = np.count_nonzero(u != (P & 255)); dv = np.count_nonzero(v != ((P >> 8) & 255))
    # distance to the nearest rounding boundary where they differ
    fu = np.abs(g[..., 0] - np.floor(g[..., 0]) - 0.5)[u != (P & 255)]
    fv = np.abs(g[..., 1] - np.floor(g[..., 1]) - 0.5)[v != ((P >> 8) & 255)]
    print(f'GLSL jzPolar vs [0x16f4b] (t430): u differs in {du} of 204000 entries (max distance to a .5 boundary {fu.max() if du else 0:.4f}), v in {dv} ({fv.max() if dv else 0:.4f})')
    sp = np.fromfile(D + '/spot.f32', np.float32).reshape(256, 256, 4)[..., 0].astype(np.float64)
    ds = np.count_nonzero(np.rint(sp) != S)
    fs = np.abs(sp - np.floor(sp) - 0.5)[np.rint(sp) != S]
    print(f'GLSL jzSpotMap vs [0x654a] (t430): {ds} of 65536 differ after rounding (max distance to a .5 boundary {fs.max() if ds else 0:.4f}); max |value - table| {np.abs(sp - S).max():.3f}')


if __name__ == '__main__':
    {'rng': check_rng, 'glow': check_glow, 'tables': check_tables}[sys.argv[1]](sys.argv[2])
