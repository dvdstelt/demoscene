"""Model of the intro (light drops over intro/intro.raw), checked against a run resumed from snaps/f38.snap with
hooks_intro.py.

- tick(T, X, Y, seed): the tick routine 0x403214 (once per refresh).
- frame(...): the frame routine 0x403748: drops 0x40337c, blur 0x403674, picture 0x415df0 (type 3: index 0
  transparent), shade 0x4036e8.
Checks: every tick against tick_in/tick_out; every frame's drops, seed and light buffer against f_drops / f_blur
(frames with a tick inside the drop loop are replayed with the tick at each possible drop index); the shaded frame
against f_shaded; then the whole chain from the first dumped frame on, events in the captured order.

usage: python3 intro_model.py <run>.intro.pkl
"""
import os, sys, math, pickle, struct
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..'))
import datfile

DAT = datfile.Dat(os.path.join(HERE, '..', '..', '..', 'original', 'INSIDE.DAT'))
PIC = np.frombuffer(DAT.get('intro/intro.raw'), np.uint8).reshape(200, 320)
SHD = np.frombuffer(DAT.get('intro/intro.shd'), np.uint8).reshape(64, 256)
F32 = lambda v: struct.unpack('<f', struct.pack('<f', v))[0]


class Rng:
    def __init__(self, seed): self.seed = seed
    def __call__(self):
        self.seed = (self.seed * 0x41c64e6d + 0x3039) & 0xffffffff
        return (self.seed >> 16) & 0x7fff


def tick(T, X, Y, rng):
    """0x403214: the light source wanders; T = part ticks [0x4248c8] (4 per refresh)."""
    X = 10.0 * math.sin(T * 0.02) * 4.7 + math.cos(T * 0.02320185614849188) + math.sin(T * 0.03690036900369004) * 4.4 + X
    Y = 12.0 * math.cos(T * 0.03184713375796178) * 3.7 + math.cos(T * 0.027700831024930747) + \
        math.sin(T * 0.024330900243309004) * 2.6 + Y
    if X > 319.0: X = float(rng() % 40 + 30)
    if 0.0 > X: X = float(rng() % 50 + 250)
    if Y > 199.0: Y = float(rng() % 20 + 30)
    if 0.0 > Y: Y = float(rng() % 20 + 160)
    return X, Y


def drops(D, L, count, flag, src, rng, split=None):
    """0x40337c. D: list of [x (float32), y (float32), intensity (int)]; L: int32 array (200, 320) holding the
    16-bit light values; src: dict with X, Y (doubles, may change at drop index split: (i, tick function))."""
    if flag > 0 and count < 128:
        count += 1
    for i in range(count):
        if split is not None and split[0] == i:
            split[1]()
        d = D[i]
        if rng() % 20 == 0:
            xi = math.trunc(rng() % 90 + src['X'] + -40.0)
            y = float(math.trunc(rng() % 80 + src['Y'] + -40.0))
            d[0], d[1] = F32(float(xi)), F32(y)
            d[2] = rng() % 6500 + count * 100
            if (1 if rng() == 0 else 0) % 3 != 0:     # !rand() % 3: true only when rand() returns 0
                d[2] = -d[2]
            d[2] += (count - 22) * 30
        if 0.0 > d[0]: d[0] = float(rng() % 50 + 260)
        if 0.0 > d[1]: d[1] = float(rng() % 30 + 160)
        if d[0] > 319.0: d[0] = float(rng() % 30 + 50)
        if d[1] > 199.0: d[0] = float(rng() % 30 + 45)  # (sic) sets x
        x, y = d[0], d[1]
        if 1.0 > x or x >= 319.0 or 1.0 > y or y >= 199.0:
            continue
        if math.sqrt((80.0 - y) ** 2 + (80.0 - x) ** 2) < 60.0:
            continue
        yi, xi = math.trunc(y), math.trunc(x)
        L[yi, xi] = min(max(int(L[yi, xi]) + d[2], -30000), 30000)
    if split is not None and split[0] >= count:
        split[1]()
    return count


def tdiv8(a):
    """C division by 8 (toward zero)."""
    return np.where(a < 0, -((-a) // 8), a // 8)


def blur(L):
    """0x403674 in place, rows 1..197, columns 1..318, raster order: anti-diagonal wavefronts."""
    for s in range(2, 197 + 318 + 1):
        y = np.arange(max(1, s - 318), min(197, s - 1) + 1)
        x = s - y
        v = 4 * L[y, x] + L[y, x - 1] + L[y, x + 1] + L[y - 1, x] + L[y + 1, x]
        L[y, x] = tdiv8(v)


def shade(L, old):
    """0x415df0 (intro.raw, type 3: index 0 keeps the old pixel) then 0x4036e8."""
    pic = np.where(PIC != 0, PIC, old)
    return SHD[np.clip(L, 0, 63), pic]


def unpack_drops(b):
    a = np.frombuffer(b, np.uint8).reshape(130, 16)
    f = a[:, :8].copy().view(np.float32).reshape(130, 2)
    i = a[:, 8:12].copy().view(np.int32).reshape(130)
    return [[float(f[k, 0]), float(f[k, 1]), int(i[k])] for k in range(130)]


def light(b):
    return np.frombuffer(b, np.int16).reshape(200, 320).astype(np.int64)


def main():
    ev = pickle.load(open(sys.argv[1], 'rb'))
    # 1. ticks
    bad = 0; nt = 0; rngs = 0
    pend = None
    for e in ev:
        if e['kind'] == 'tick_in':
            pend = e
        elif e['kind'] == 'tick_out':
            r = Rng(pend['seed'])
            X, Y = tick(pend['T'], pend['X'], pend['Y'], r)
            nt += 1
            rngs += r.seed != pend['seed']
            if not (abs(X - e['X']) < 1e-9 and abs(Y - e['Y']) < 1e-9 and r.seed == e['seed']):
                bad += 1
                if bad < 5: print('tick mismatch', pend['T'], X, e['X'], Y, e['Y'])
    print('ticks: %d checked, %d mismatches, %d called rand' % (nt, bad, rngs))
    Ts = sorted({e['T'] % 4 for e in ev if e['kind'] == 'tick_in'})
    print('tick routine T mod 4:', Ts, ' first tick T', next(e['T'] for e in ev if e['kind'] == 'tick_in'),
          'at refresh', next(e['refresh'] for e in ev if e['kind'] == 'tick_in'))
    # 2. frames
    idx = {}
    for i, e in enumerate(ev):
        if e['kind'] in ('f_entry', 'f_drops', 'f_blur', 'f_shaded'):
            idx.setdefault((e['n'], e['kind']), i)
    nf = ok_d = ok_b = ok_s = with_tick = 0
    for n in range(0, 2000):
        if (n, 'f_entry') not in idx or (n, 'f_drops') not in idx:
            continue
        a, b = idx[(n, 'f_entry')], idx[(n, 'f_drops')]
        e0, e1 = ev[a], ev[b]
        inner = [x for x in ev[a:b] if x['kind'] == 'tick_in']
        nf += 1
        cands = [None] if not inner else list(range(0, e0['count'] + 2))
        with_tick += bool(inner)
        good = False
        for sp in cands:
            D = unpack_drops(e0['drops']); r = Rng(e0['seed']); src = {'X': e0['X'], 'Y': e0['Y']}
            L = light(e0['light']) if 'light' in e0 else None
            Lm = L if L is not None else np.zeros((200, 320), np.int64)
            split = None
            if sp is not None:
                t = inner[0]
                def do(t=t, src=src, r=r):
                    src['X'], src['Y'] = tick(t['T'], src['X'], src['Y'], r)
                split = (sp, do)
            drops(D, Lm, e0['count'], e0['flag'], src, r, split)
            if D == unpack_drops(e1['drops']) and r.seed == e1['seed'] and abs(src['X'] - e1['X']) < 1e-6 and abs(src['Y'] - e1['Y']) < 1e-6 and (L is None or 'light' not in e1 or (Lm == light(e1['light'])).all()):
                good = True
                break
        ok_d += good
        if 'light' in e1 and (n, 'f_blur') in idx and 'light' in ev[idx[(n, 'f_blur')]]:
            Lb = light(e1['light']).copy(); blur(Lb)
            ok_b += (Lb == light(ev[idx[(n, 'f_blur')]]['light'])).all()
            if 'target' in e0 and (n, 'f_shaded') in idx and 'target' in ev[idx[(n, 'f_shaded')]]:
                old = np.frombuffer(e0['target'], np.uint8).reshape(200, 320)
                got = np.frombuffer(ev[idx[(n, 'f_shaded')]]['target'], np.uint8).reshape(200, 320)
                ok_s += (shade(Lb, old) == got).all()
    print('frames: %d; drops+seed (+light where dumped) exact in %d (%d had a tick inside the drop loop); '
          'blur exact in %d; shaded frame exact in %d' % (nf, ok_d, with_tick, ok_b, ok_s))


if __name__ == "__main__" and "--chain" not in sys.argv:
    main()


def event_3_00(D, rng):
    """0x4031a0 (script 3:00): [0x425570] = 1 and every drop to a random place with intensity 7000."""
    for d in D:
        d[0] = float(rng() % 320)
        d[1] = float(rng() % 200)
        d[2] = 7000
    return 1


def chain(ev):
    """Replay the whole run from the first dumped frame: ticks, frames and the 3:00 event in the captured order."""
    first = next(i for i, e in enumerate(ev) if e['kind'] == 'f_entry')
    e0 = ev[first]
    D = unpack_drops(e0['drops']); rng = Rng(e0['seed']); src = {'X': e0['X'], 'Y': e0['Y']}
    count, flag = e0['count'], e0['flag']
    L = light(e0['light']).copy()
    bufs = {}
    i = first
    checked = {'light': 0, 'light_ok': 0, 'drops': 0, 'drops_ok': 0, 'shaded': 0, 'shaded_ok': 0}
    in_frame = False
    pending_tick = None
    while i < len(ev):
        e = ev[i]
        k = e['kind']
        if k == 'f_entry':
            in_frame = True
            # a tick inside the drop loop: find it before f_drops
            j = i + 1
            inner = None
            while ev[j]['kind'] != 'f_drops':
                if ev[j]['kind'] == 'tick_in': inner = ev[j]
                j += 1
            eD = ev[j]
            best = None
            for sp in ([None] if inner is None else range(0, count + 2)):
                D2 = [d[:] for d in D]; r2 = Rng(rng.seed); s2 = dict(src); L2 = L.copy()
                split = None
                if sp is not None:
                    def do(t=inner, s2=s2, r2=r2): s2['X'], s2['Y'] = tick(t['T'], s2['X'], s2['Y'], r2)
                    split = (sp, do)
                c2 = drops(D2, L2, count, flag, s2, r2, split)
                if r2.seed == eD['seed'] and D2 == unpack_drops(eD['drops']) and abs(s2['X'] - eD['X']) < 1e-6 and abs(s2['Y'] - eD['Y']) < 1e-6:
                    best = (D2, r2, s2, L2, c2)
                    break
            if best is None:
                print('chain broke at frame', e['n']); return checked
            D, rng, src, L, count = best[0], best[1], best[2], best[3], best[4]
            rng = Rng(rng.seed)
            checked['drops'] += 1; checked['drops_ok'] += 1
            if 'light' in eD:
                checked['light'] += 1; checked['light_ok'] += (L == light(eD['light'])).all()
            i = j + 1
            continue
        if k == 'tick_in':
            if not in_frame or True:
                # ticks inside a drop loop were consumed above (skip the duplicate)
                pass
            i2 = i
        if k == 'tick_in' and not getattr(e, 'used', False):
            # skip ticks that were inside a drop loop (between f_entry and f_drops)
            prev = max(x for x in range(i) if ev[x]['kind'] in ('f_entry', 'f_drops'))
            if ev[prev]['kind'] == 'f_entry':
                i += 1; continue
            src['X'], src['Y'] = tick(e['T'], src['X'], src['Y'], rng)
        elif k == 'f_blur' and e['n'] >= 0:
            blur(L)
            if 'light' in e:
                checked['light'] += 1; checked['light_ok'] += (L == light(e['light'])).all()
        elif k == 'f_shaded' and e['n'] >= 0:
            n = e['n']
            old = bufs.get(n - 2)
            if old is None and 'target' in ev[idx_entry(ev, n)]:
                old = np.frombuffer(ev[idx_entry(ev, n)]['target'], np.uint8).reshape(200, 320)
            if old is not None:
                out = shade(L, old)
                bufs[n] = out
                if 'target' in e:
                    checked['shaded'] += 1; checked['shaded_ok'] += (out == np.frombuffer(e['target'], np.uint8).reshape(200, 320)).all()
        elif k == 'ev_4031a0':
            flag = event_3_00(D, rng)
        i += 1
    return checked


_IDX = {}
def idx_entry(ev, n):
    if not _IDX:
        for i, e in enumerate(ev):
            if e['kind'] == 'f_entry': _IDX[e['n']] = i
    return _IDX[n]


if __name__ == '__main__' and len(sys.argv) > 2 and sys.argv[2] == '--chain':
    print('chain:', chain(pickle.load(open(sys.argv[1], 'rb'))))
