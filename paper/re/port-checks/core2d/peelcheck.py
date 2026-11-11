import math, numpy as np
def sinT(k):
    k &= 1023
    if k == 661: return -51912
    if k == 905: return -43712
    return int(round(65536*math.sin(2*math.pi*k/1024)))
def cosT(k): return sinT(k + 256)
def recip(n): return 65536 // n if n > 0 else 0
def quad(p0, p1, p2, n):
    step = recip(n) >> 7; B = 4*p1 - p2 - 3*p0; A = p0 - 2*p1 + p2
    out = []
    for k in range(n+1):
        t = k*step
        out.append((((B*t + ((A*t*t) >> 8)) >> 8) + 2*p0) >> 1)
    return out
def peel_exact_rows(p):
    """class per screen row: 0 new, 1 old front, 2 back, 3 stale"""
    A = quad((cosT(p)*200) >> 16, (cosT(p >> 1)*100) >> 16, 0, 25)
    Bz = quad((sinT(-p & 1023)*1600) >> 16, (sinT((-p >> 1) & 1023)*800) >> 16, 0, 25)
    rows = [3]*200
    ymax, ymin = 0, 200
    for i in range(25):
        ymax = max(ymax, A[i]); ymin = min(ymin, A[i])
        y1, y0 = A[i+1], A[i]
        if y1 == y0: continue
        if y1 > y0:
            top, bot, cls = y0, y1, 2
            if bot <= 0 or top >= 200: continue
            top = max(top, 0); bot = min(bot, 200)
        else:
            top, bot, cls = y1, y0, 1
            if bot <= 0 or top >= ymin: continue
            top = max(top, 0); bot = min(bot, ymin)
        for r in range(top, bot): rows[r] = cls
    if ymax < 200:
        for r in range(ymax, 200): rows[r] = 0
    return rows
def peel_cont_rows(p):
    a0 = 200*math.cos(2*math.pi*p/1024); a1 = 100*math.cos(2*math.pi*p/2048)
    Bq = 4*a1 - 3*a0; Aq = a0 - 2*a1
    a = lambda s: a0 + 0.5*(Bq*s + Aq*s*s)
    rows = []
    for r in range(200):
        y = r + 0.5
        qa, qb, qc = 0.5*Aq, 0.5*Bq, a0 - y
        roots = []
        if abs(qa) < 1e-9:
            if abs(qb) > 1e-12: roots = [-qc/qb]
        else:
            d = qb*qb - 4*qa*qc
            if d >= 0: roots = [(-qb - math.sqrt(d))/(2*qa), (-qb + math.sqrt(d))/(2*qa)]
        best, kind = -1, 0
        for s in roots:
            if s < 0 or s > 2 or s <= best: continue
            back = qb + 2*qa*s >= 0
            if not back:
                m = a(0); v = -qb/(2*qa) if qa else -1
                if qa > 0 and 0 < v < s: m = min(m, a(v))
                if y > m + 1e-9: continue
            kind = 2 if back else 1; best = s
        rows.append(kind)
    return rows
def prog(t, first=5483):
    k = t - first + 1
    if k <= 0: return 0
    return 4*k if k <= 64 else min(700, 256 + 7*(k-64))
FR = 64768
data = np.memmap('/home/dennis/Downloads/acme_303/paper/re/full.frames', dtype=np.uint8, mode='r')
def frame(t): return np.asarray(data[t*FR:t*FR+64000]).reshape(200, 320)
final = frame(5615)
def cap_rows(t):
    f = frame(t); out = []
    for r in range(200):
        row = f[r]
        if np.array_equal(row, final[r]): out.append(0)
        elif np.mean((row >= 28) & (row <= 31)) > 0.9: out.append(2)
        else: out.append(1)
    return out
def runs(rows):
    s = []; prev = None
    for r, c in enumerate(rows):
        if c != prev: s.append((r, c)); prev = c
    return s
tot_bad = {}
for t in range(5484, 5612, 1):
    cap = cap_rows(t)
    best = None
    for lag in (0, 1, 2):
        ex = peel_exact_rows(prog(t - lag))
        # stale rows (3) are whatever was there: count them as matching anything
        bad = sum(1 for r in range(200) if ex[r] != 3 and ex[r] != cap[r] and not (ex[r] == 1 and cap[r] == 0))
        if best is None or bad < best[0]: best = (bad, lag)
    tot_bad.setdefault(best[1], []).append((t, best[0]))
    if t % 8 == 0:
        lag = best[1]
        co = peel_cont_rows(prog(t - lag))
        ex = peel_exact_rows(prog(t - lag))
        dif = sum(1 for r in range(200) if co[r] != ex[r] and ex[r] != 3)
        print('t %d p %d lag %d exact-vs-capture bad rows %d | cont-vs-exact differing rows %d | exact %s | cont %s' % (t, prog(t-lag), lag, best[0], dif, runs(ex), runs(co)))
for lag, v in sorted(tot_bad.items()):
    print('lag', lag, 'ticks', len(v), 'max bad rows', max(b for _, b in v))
