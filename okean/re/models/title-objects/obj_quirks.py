"""statistics over every draw of part 2 (state from the callback model and the logged positions):
vertices with z <= 0 (the degenerate projection), lines whose stamp count is cut to its low byte (slow path,
n > 255) and that still put stamps on screen, frames whose index sum passes 255 (wrap and dword carries)"""
import sys, copy, pickle
from vlog import *
import objects as O
from objects import *
ev = parse('vlog2.txt')
s = snap('2e779')
st = init_from_snapshot(s)
cbs = [(d['b12b75'], d['w12b73']) for a, f, irq, t, d in ev if a == '2eb81']
ds = draws(ev)

def line_info(x1, y1, x2, y2, shift):
    """(n, count actually drawn, stamps on screen) like O.line"""
    st_ = []
    O.line(st_, x1, y1, x2, y2, shift)
    return len(st_)

def zs(stt):
    out = []
    for (x, y, z) in VERTS:
        x, y = rot(x, y, stt['angA']); x, z = rot(x, z, stt['angB'])
        x = s32(x + stt['xoff'] + stt['xoff2']); x, z = rot(x, z, stt['angC']); y, z = rot(y, z, stt['angD'])
        out.append(s32(z + stt['zdist']))
    return out

done = 0
rep = []
for r in ds[::int(sys.argv[1]) if len(sys.argv) > 1 else 1]:
    c0 = r['irq0'] - 617
    while done < c0:
        callback(st, *cbs[done]); done += 1
    order = r['bg']['b12b75']
    sd = copy.deepcopy(st)
    objs = []
    if order < 3:
        sd['angC'] = sd['angC0']
        for k in range(8):
            objs.append(dict(sd)); sd['angC'] = (sd['angC'] + 0x1ff) & 0xfff
    else:
        sd['xoff2'] = s32(sd['tubeX'] - 0x5000)
        for k in range(5):
            objs.append(dict(sd)); sd['xoff2'] = s32(sd['xoff2'] + 0x2000)
    nzneg = 0; trunc_vis = 0; neg_vis = 0; total = 0
    for o in objs:
        P = project(o); Z = zs(o)
        nzneg += sum(1 for z in Z if z <= 0)
        for a, b in EDGES:
            x1, y1 = P[a]; x2, y2 = P[b]
            stamps = []
            O.line(stamps, x1, y1, x2, y2, o['shift'])
            total += len(stamps)
            # was the count cut? recompute n
            d = s32(x2 - x1); n = (d ^ (d >> 31)) + 1; d2 = s32(y2 - y1); m = (d2 ^ (d2 >> 31)) + 1
            n = max(n & M32, m & M32) >> o['shift']
            if n > 255 and stamps:
                trunc_vis += len(stamps)
            if (Z[a] <= 0 or Z[b] <= 0) and stamps:
                neg_vis += len(stamps)
    rep.append((r['frame'], order, nzneg, trunc_vis, neg_vis, total))
    if nzneg or trunc_vis:
        print(r['frame'], 'order', order, 'z<=0 vertices', nzneg, 'stamps of cut lines', trunc_vis, 'stamps of lines to z<=0', neg_vis, 'all stamps', total, flush=True)
pickle.dump(rep, open('quirks2.pkl', 'wb'))
