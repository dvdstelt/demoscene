"""Python model of the water part's per-frame update (0x4078d4: 0x407400 per fish, the random drops, the ripple
0x401234) and of the obuke's rotation (0x407c78), checked against a run of run_inside.py --water-dump.

usage: python3 water_model.py run1      (run1.pkl from models/water/run_inside.py --water-dump)"""
import math
import pickle
import struct
import sys

import numpy as np

sys.path.insert(0, '../..')
import datfile

W, H = 320, 200
DAT = datfile.Dat('../../../original/INSIDE.DAT')
PIC = np.frombuffer(DAT.get('water/water.raw'), np.uint8)
PI = 3.1415926536             # the code's pi (0x400921fb544486e0 = 3.1415926536), not math.pi
DEG = 0.01745329252           # 0x421304 / 0x421396


class Rand:
    def __init__(self, seed): self.seed = seed
    def __call__(self):
        self.seed = (self.seed * 0x41c64e6d + 0x3039) & 0xffffffff
        return (self.seed >> 16) & 0x7fff


def trunc(x): return int(math.trunc(x))


def rot_y(m, a):
    """0x414344 (via 0x4145ac): M = M x Ry(a), Ry = [[c,0,s],[0,1,0],[-s,0,c]], M row-major 3x3."""
    c, s = math.cos(a), math.sin(a)
    m = list(m)
    for r in range(3):
        m0, m2 = m[3 * r], m[3 * r + 2]
        m[3 * r], m[3 * r + 2] = m0 * c - m2 * s, m0 * s + m2 * c
    return m


def mat_vec(m, v):
    """0x4144ec: v' = M v (M row-major)."""
    return [m[3 * r] * v[0] + m[3 * r + 1] * v[1] + m[3 * r + 2] * v[2] for r in range(3)]


def project(p):
    """0x411e80 with the water scene: camera at (0, 0, -1600), identity rotation, centre (160, 100),
    scales [scene+0x18] = 32768, [scene+0x1c] = 27306 (/16384), near plane [scene+0x44] / 256 = 28."""
    x, y, z = p[0] - 0.0, p[1] - 0.0, p[2] + 1600.0
    if 0x1c00 / 256 > z:
        return None
    return 160 + 32768 / 16384 * (x * 128) / z, 100 + 27306 / 16384 * (y * 128) / z


def fish_update(i, fish, c, rnd, buf, log):
    """0x407400(i). fish = {'pos': [x,y,z] ints, 'mat': 9 doubles, 'verts': [[16 ints] x 8]}; buf = int16 array
    (the height field the frame writes into, 'water frame 1' [0x4265e4])."""
    a = (math.sin((c + 171 * i) * 0.001584786053882726) + math.cos((c - 57 * i) * 0.0027019724398811133)
         + math.cos(-c * 0.008130081300813007) * 0.1)
    fish['mat'] = rot_y(fish['mat'], a * DEG)
    v = fish['verts']
    v[4][2] = trunc(-math.sin(c * 0.022222222222222223) * 15.0)     # vertex 4 x       (obj+0xd0)+0x108
    v[5][2] = trunc(-math.sin(c * 0.02702702702702703) * 20.0)      # vertex 5 x       +0x148
    v[6][2] = trunc(math.sin(c * 0.01639344262295082) * 25.0)       # vertex 6 x       +0x188
    v[4][13] = trunc(math.sin(c * 0.022222222222222223) * 800.0)    # vertex 4 normal x +0x134
    v[5][14] = trunc(math.sin(c * 0.02702702702702703) * 800.0)     # vertex 5 normal y +0x178
    v[6][15] = trunc(math.sin(c * 0.01639344262295082) * 800.0)     # vertex 6 normal z +0x1bc
    s = a * 30.0 + 10.0
    if s < 15.0:
        s = 15.0
    d = mat_vec(fish['mat'], [0.0, 0.0, s])                         # 0x413d58: pos += M (0,0,s) (length s)
    p = [fish['pos'][k] + d[k] for k in range(3)]
    fish['pos'] = [trunc(p[k]) for k in range(3)]
    sp = project(p)                                                  # the double position, before truncation
    log['a'].append(a); log['s'].append(s); log['proj'].append((i,) + sp)
    sx, sy = sp
    if sx < -70.0: fish['mat'] = rot_y(fish['mat'], PI)
    if sx > 390.0: fish['mat'] = rot_y(fish['mat'], PI)
    if sy < -70.0: fish['mat'] = rot_y(fish['mat'], PI)
    if sy > 270.0: fish['mat'] = rot_y(fish['mat'], PI)
    r = rnd()
    h = 600.0
    if r % 300 == 0:
        h = float(rnd() % 40000)
    if sx > 3.0 and W - 3 > sx and sy > 3.0 and H - 3 > sy:
        hv = np.int16(np.uint16(trunc(h) & 0xffff).view(np.int16))
        for dx in range(-3, 3):
            for dy in range(-3, dx):
                buf[(trunc(sy) + dy) * W + trunc(sx) + dx] = hv


def drops(rnd, buf, count=3, chance=200):
    """0x4078d4 after the fish: [0x422854] = 3 tries, each with probability 1/[0x422858] = 1/200."""
    for _ in range(count):
        if rnd() % chance == 0:
            x = rnd() % (W - 20) + 10
            y = rnd() % (H - 20)
            h = rnd() % 20000
            for dx in range(-3, 4):
                for yy in range(y + 7, y + 14):
                    buf[yy * W + x + dx] = h


def ripple(cur, prev, pic=PIC):
    """0x401234: cur (int16, written in place) = damped(neighbours(prev) / 2 - cur), clamped to +-0x3fff;
    returns the background rows 2..197 (bytes, index 640..63359 of the frame)."""
    cur = cur.astype(np.int32)
    prev = prev.astype(np.int32)
    out_cur = cur.copy()
    n = (H - 4) * W
    p = W + np.arange(n)                                         # height-field index: rows 1..196
    v = (prev[p - 1] + prev[p + 1] + prev[p - W] + prev[p + W]) >> 1
    v = v - cur[p]
    v = v - (v >> 4)
    v = np.clip(v, -0x3fff, 0x3fff)
    out_cur[p] = v
    d = -(v >> 5)
    dst = pic[p + W + d]                                         # picture and screen are one row lower
    return out_cur.astype(np.int16), dst


def check(run):
    D = pickle.load(open(run + '.pkl', 'rb'))
    F = D['frames']
    i16 = lambda b: np.frombuffer(b, np.int16).copy()
    # 1. ripple, exactly
    for k, f in enumerate(F):
        if 'cur_out' not in f or 'dst_bg' not in f:
            continue
        cur, dst = ripple(i16(f['cur_in']), i16(f['prev_in']))
        bg = np.frombuffer(f['dst_bg'], np.uint8)
        print('frame %d: ripple height field %s, background rows 2..197 %s, rows 0-1/198-199 zero %s' % (
            k, 'exact' if np.array_equal(cur, i16(f['cur_out'])) else 'DIFFERS',
            'exact' if np.array_equal(dst, bg[640:640 + len(dst)]) else 'DIFFERS',
            not bg[:640].any() and not bg[63360:].any()))
    # 2. fish + drops: from the state at frame k predict frame k+1 (positions, matrices, seed, height writes).
    # Each fish reads the interrupt counter [0x4248c4] itself, so a timer interrupt during the 8 updates gives the
    # later fish c + 1: try c, c + 1, c + 2 per fish and report how often that was needed.
    stats = {'late_c': 0, 'drops': 0, 'big_wakes': 0, 'turns': 0, 'wakes': 0, 'frames': 0}
    bad = 0
    for k in range(len(F) - 1):
        f, g = F[k], F[k + 1]
        if 'seed_drops' not in f:
            continue
        rnd = Rand(f['seed'])
        full = k > 0 and 'cur_in' in f and 'prev_in' in F[k - 1]
        buf = i16(F[k - 1]['prev_in']) if full else np.zeros(W * H, np.int16)
        ok = True
        for i in range(8):
            for dc in range(3):
                x = {'pos': list(f['fish'][i]['pos']), 'mat': list(f['fish'][i]['mat']),
                     'verts': [list(v) for v in f['fish'][i]['verts']]}
                r2 = Rand(rnd.seed)
                b2 = buf.copy()
                log = {'a': [], 's': [], 'proj': []}
                fish_update(i, x, f['c'] + dc, r2, b2, log)
                if x['pos'] == list(g['fish'][i]['pos']) and x['mat'] == list(g['fish'][i]['mat']) and all(
                        x['verts'][v][j] == g['fish'][i]['verts'][v][j] for v in (4, 5, 6) for j in (2, 13, 14, 15)):
                    break
            else:
                ok = False
                print('frame %d fish %d: no single c in c..c+2 reproduces it (c changed inside its update)' % (k, i))
            stats['late_c'] += dc > 0
            stats['big_wakes'] += (r2.seed != Rand(rnd.seed).seed and False)
            sx, sy = log['proj'][0][1:]
            stats['turns'] += (sx < -70) + (sx > 390) + (sy < -70) + (sy > 270)
            stats['wakes'] += (3 < sx < W - 3 and 3 < sy < H - 3)
            n_calls = 0
            t = Rand(rnd.seed)
            while t.seed != r2.seed:
                t(); n_calls += 1
            stats['big_wakes'] += n_calls == 2
            rnd, buf = r2, b2
        s0 = rnd.seed
        drops(rnd, buf)
        t = Rand(s0); n_calls = 0
        while t.seed != rnd.seed:
            t(); n_calls += 1
        stats['drops'] += (n_calls - 3) // 3
        stats['frames'] += 1
        if rnd.seed != f['seed_drops'] or rnd.seed != g['seed']:
            ok = False
        if full and not np.array_equal(buf, i16(f['cur_in'])):
            ok = False
            print('frame %d: height writes differ' % k)
        bad += not ok
    print('fish and drops: %d of %d frames differ; %s' % (bad, stats['frames'], stats))
    # 3. the obuke's rotation per refresh (0x407c78): angle about y = 10 deg + 0.0185 rad per call
    T = D['ticks']
    m0 = F[0]['obj_mat'] if F else None
    worst = 0.0
    for n, (refresh, now, c, m) in enumerate(T):
        ang = math.atan2(m[2], m[0])
        worst = max(worst, abs(m[1]) + abs(m[3]) + abs(m[5]) + abs(m[7]) + abs(m[4] - 1))
    angs = [math.atan2(m[2], m[0]) for (_, _, _, m) in T]
    steps = [((b - a + math.pi) % (2 * math.pi)) - math.pi for a, b in zip(angs, angs[1:])]
    print('tick routine calls %d (refreshes %d..%d), rotation per call %.12f..%.12f rad, off-axis %.2g' % (
        len(T), T[0][0], T[-1][0], min(steps), max(steps), worst))
    print('angle at the first recorded tick %.9f rad = 10 deg + %.4f steps' % (
        angs[0], (angs[0] - 0.1745329252) / 0.0185))


if __name__ == '__main__':
    check(sys.argv[1] if len(sys.argv) > 1 else 'run1')
