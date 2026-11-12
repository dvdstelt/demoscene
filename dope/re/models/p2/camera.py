"""p2's camera / object motion as the per-retrace routine 2580 steps it, from the script (script.py).

state(r): the camera record 0x1810 (position x,y,z: s32 at 1816/181a/181e, velocity at 1822/1826/182a, angles a,b,c:
u16 at 182e/1830/1832, angular velocity s16 at 1834/1836/1838) and the discs' pivot x (1878) after r retraces of
p2 (r = 0: the values the script sets before the first step).
Nominal: step k starts at sum(N_0..N_{k-1}) retraces (no overshoot). --trace out/runs/p2.p2log compares with the
emulator's per-retrace log (R lines: the state at the entry of 2580, i.e. after r-1 updates).
"""
import os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import script

KEYS = {0x1816: 'x', 0x181a: 'y', 0x181e: 'z', 0x1822: 'vx', 0x1826: 'vy', 0x182a: 'vz',
        0x182e: 'a', 0x1830: 'b', 0x1832: 'c', 0x1834: 'va', 0x1836: 'vb', 0x1838: 'vc', 0x1878: 'pivot',
        0x1955: 'lp', 0x1957: 'ld', 0x193f: 'fl'}


def wrap32(v): return (v + 2**31) % 2**32 - 2**31


def simulate(starts=None):
    """per-retrace states (list index r = state after r updates); starts: actual step start retraces (optional)"""
    steps = script.parse()
    st = {k: 0 for k in KEYS.values()}
    st['pivot'] = 6000
    st['fv'] = 0
    out, r = [], 0
    nominal = []
    acc = 0
    for i, (a, n, w) in enumerate(steps):
        nominal.append(acc); acc += n
    if starts and len(starts) < len(steps):            # partial log: later steps keep the last delay
        starts = list(starts) + [nominal[i] + starts[-1] - nominal[len(starts) - 1]
                                 for i in range(len(starts), len(steps))]
    for i, (a, n, w) in enumerate(steps):
        s0 = starts[i] if starts else nominal[i]
        while r < s0:                                  # retraces of the previous step
            out.append(dict(st)); step(st); r += 1
        for k, v in w:
            if k in KEYS:
                st[KEYS[k]] = v & 0xffff if KEYS[k] in 'abc' else v
    end = (starts[-1] + steps[-1][1]) if starts else acc
    while r <= end:
        out.append(dict(st)); step(st); r += 1
    return out


def step(st):
    # 2580: label slide, white fade, then the motion of the camera record
    st['lp'] += st['ld']
    if st['lp'] < 0 or st['lp'] >= 0x4b:
        st['ld'] = 0
    if st['fl'] == 1:
        if st['fv'] >= 0x3f00:
            st['fl'] = 0; st['fv'] = 0x3e7f
        st['fv'] = (st['fv'] + 0xff) & 0xffff
    for p, v in (('x', 'vx'), ('y', 'vy'), ('z', 'vz')):
        st[p] = wrap32(st[p] + st[v])
    for p, v in (('a', 'va'), ('b', 'vb'), ('c', 'vc')):
        st[p] = (st[p] + st[v]) & 0xffff


def read_trace(path):
    R, S = {}, []
    for line in open(path):
        f = line.split()
        if f[0] == 'R':
            v = list(map(int, f[1:]))
            R[v[0]] = dict(zip(('x', 'y', 'z', 'vx', 'vy', 'vz', 'a', 'b', 'c', 'va', 'vb', 'vc', 'pivot',
                                'lp', 'ld', 'fl', 'fv'), v[2:19]))
        elif f[0] == 'S':
            S.append(int(f[3]))
    return R, S


if __name__ == '__main__':
    if len(sys.argv) > 2 and sys.argv[1] == '--trace':
        R, S = read_trace(sys.argv[2])
        steps = script.parse()
        acc, nom = 0, []
        for a, n, w in steps:
            nom.append(acc); acc += n
        late = [(i, S[i] - nom[i]) for i in range(len(S)) if S[i] != nom[i]]
        print('steps', len(S), 'starting later than nominal (step, retraces late):', late[:40])
        sim = simulate(S)
        bad = 0
        for r, t in sorted(R.items()):
            m = sim[r - 1]
            d = [k for k in t if t[k] != m[k]]
            if d:
                bad += 1
                if bad < 5: print('retrace', r, 'differs', d, t, m)
        print('retraces compared', len(R), 'differing', bad)
    else:
        sim = simulate()
        steps = script.parse(); acc = 0
        for i, (a, n, w) in enumerate(steps):
            s = sim[acc]
            print('%3d r=%5d z=%9d vz=%6d a=%5d b=%5d c=%5d va=%3d vb=%3d vc=%4d pivot=%d' % (
                i, acc, s['z'], s['vz'], s['a'], s['b'], s['c'], s['va'], s['vb'], s['vc'], s['pivot']))
            acc += n
        s = sim[acc]; print('end r=%d z=%d a=%d b=%d c=%d' % (acc, s['z'], s['a'], s['b'], s['c']))
