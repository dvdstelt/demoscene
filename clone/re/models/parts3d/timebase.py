"""Time base of the 3D parts from a run_clone.py --p3d dump of a whole part: every dt-integrated variable against
the closed form rate * (t70 - t70_origin) (t70 = timer / 17043), t70_origin = the timer of the frame before the part's first frame; for
part 9 the three angles against piecewise-linear closed forms with the switch at (a) the timer of the frame before
the event frame (what the original does) and (b) the music time of the event (what a remake would use).

usage: python3 timebase.py P.pkl P.ticks
"""
import os, sys, pickle
import numpy as np
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
from sheet import read_ticks
PIT = 1193182
recs = [r for r in pickle.load(open(sys.argv[1], 'rb')) if 'after' in r]
ticks = read_ticks(sys.argv[2])
part = recs[0]['part']
U = 17043.0                       # one t70 unit = 17043 PIT clocks (0x41a128 = 1/17043, notes-core.md 5.1)
t70 = np.array([r['timer'] / U for r in recs])
dts = np.array([r['dt'] for r in recs])
# the part's origin = the timer of the last frame of the previous part, from the capture's .flips (the runs resume
# from snapshots taken after the part's first frame)
from sheet import read_flips
full, _ = read_flips(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'full.flips'))
fl = sorted(full)
first = next(f for f in fl if full[f][4] == part)
origin = full[first - 1][2] / U
assert abs(full[recs[0]['flip']][2] / U - t70[0]) < 1e-3, 'run does not follow the capture'
print('part %d: %d frames, t70 %.3f .. %.3f, origin %.4f (first dt %.3f)' % (part, len(recs), t70[0], t70[-1], origin, dts[0]))
print('  part starts at flip %d (t70 %.4f); origin = previous frame t70 %.4f' % (first, full[first][2] / U, origin))
print('  per-frame dt - (t70 - previous t70): max |diff| %.2e; sum of dt - (t70 - first t70) at the end: %.3f' % (
    np.abs(dts[1:] - np.diff(t70)).max(), np.cumsum(dts[1:])[-1] - (t70[-1] - t70[0])))
upto = len(recs)
if part == 7:
    upto = int(np.argmax(np.array([r['ints'][0x427f74] for r in recs]) >= 1)) or len(recs)
lin = {7: [(0xf613f4, 0.03, 'phase'), (0x427f80, 0.0045, 'fade (clamped at 1)')],
       8: [(0xf72d70, 0.05, 'tile time'), (0xf72d6c, 0.0075, 'angle'), (0x427ffc, 0.03, 'phase')],
       9: [(0xf73588, 0.02, 'colour phase x'), (0xf7358c, 0.03, 'colour phase y'), (0xf73590, 0.04, 'colour phase z'),
           (0xf735a0, 0.2, 'blob time')],
       10: [(0xf73920, 0.002, 'angle'), (0xf73924, 0.3, 'blob time'), (0xf73588, 0.02, 'colour phase x')]}
if part == 7: lin[7].insert(0, (0xf61400, 0.0075, 'angle'))
for a, rate, name in lin[part]:
    v = np.array([r['after'][a] for r in recs])
    c = (v - rate * (t70 - origin))[:upto]
    if 'fade' in name: c = c[v[:upto] < 1]
    print('  %-22s value - %.4g (t70 - origin): first %.5f, last %.5f, max |.| %.4f (value up to %.3f)' % (
        name, rate, c[0], c[-1], np.abs(c).max(), v.max()))
if part == 8:
    Z = np.array([r['after'][0x427fe4] for r in recs]); L = np.array([r['after'][0x427f70] for r in recs])
    m = Z > 1400
    print('  Z: 9400 - 75 (t70 - origin) for Z > 1400: max err %.3f; reaches 1400 at t70 %.3f' % (
        np.abs(Z[m] - (9400 - 75 * (t70[m] - origin))).max(), origin + 8000 / 75))
    F = np.array([r['after'][0x427fec] for r in recs])
    i0 = np.argmax(Z < 1400)
    print('  first frame with Z < 1400 (after event 803): flip %d t70 %.3f; fade there %.4f; light %s' % (
        recs[i0]['flip'], t70[i0], F[i0], sorted(set(np.round(L[i0:], 3)))))
    o2 = t70[i0 - 1]
    print('  tile fade 1 - 0.011 (t70 - %.3f): max err %.2e' % (o2, np.abs(F[i0:] - np.maximum(0, 1 - 0.011 * (t70[i0:] - o2))).max()))
if part == 7:
    n = np.array([r['ints'][0x427f74] for r in recs])
    for k in (1, 2, 3):
        if (n >= k).any():
            i = np.argmax(n >= k); print('  701 counter %d from flip %d (t70 %.3f)' % (k, recs[i]['flip'], t70[i]))
if part == 9:
    k = np.array([r['ints'][0x428140] for r in recs])
    A = np.array([[r['after'][a] for a in (0xf73574, 0xf73578, 0xf7357c)] for r in recs])
    sw = [i for i in range(1, len(k)) if k[i] != k[i - 1]]
    # (a) switch at the timer of the frame before the event frame
    bounds = [origin] + [t70[i - 1] for i in sw] + [1e9]
    idx = [k[0]] + [k[i] for i in sw]
    def closed(bounds, t):
        out = np.zeros(3)
        for j in range(len(idx)):
            lo, hi = bounds[j], min(bounds[j + 1], t)
            if hi > lo: out[idx[j]] += 0.003 * (idx[j] + 1) * (hi - lo)
        return out
    Aa = np.array([closed(bounds, t) for t in t70])
    print('  angles, switch at the previous frame timer: max err %.2e (angles up to %.3f)' % (np.abs(Aa - A).max(), A.max()))
    # (b) switch at the music time of the event (player timer when the row starts)
    first = {}
    for t in sorted(ticks):
        tm, order, row, timer, p, fl = ticks[t]
        first.setdefault((order, row), (timer - 22100) / U)   # .ticks: timer after the handler = row start + one tick
    ev = []
    for i in sw:
        fl = recs[i]['flip']
        # the event's position: the music position when the event frame started (from the dump's tick)
        tk = recs[i]['tick']
        o, rw = ticks[tk][1], ticks[tk][2]
        rw8 = rw - rw % 8 if True else rw
        ev.append(first.get((o, rw8), np.nan))
    bounds_b = [origin] + ev + [1e9]
    Ab = np.array([closed(bounds_b, t) for t in t70])
    print('  event times (t70):', ' '.join('%.2f' % e for e in ev[:6]), '...; switch frame timers:',
          ' '.join('%.2f' % t70[i - 1] for i in sw[:6]))
    lead = np.array(ev) - np.array([t70[i - 1] for i in sw])
    print('  the original switches %.3f .. %.3f (mean %.3f) t70 units before the event time' % (lead.min(), lead.max(), lead.mean()))
    d = Ab[-1] - A[-1]
    print('  angles at the end: original %s, with switches at the event times %s' % (np.round(A[-1], 4), np.round(Ab[-1], 4)))
    import model
    def R(Av):
        a0, a1, a2 = Av
        return model.matrix(a0 + a1 - a2, 1.1 * (a0 - a1 + a2), 0.8 * (a1 - a0 + a2), 1.0)[:, :3]
    err = [np.degrees(np.arccos(np.clip((np.trace(R(x).T @ R(y)) - 1) / 2, -1, 1))) for x, y in zip(A, Ab)]
    print('  rotation difference original vs event-time switching: max %.2f deg, at the end %.2f deg' % (max(err), err[-1]))
    fd = np.array([r['after'][0x428010] for r in recs]); e901 = np.array([r['ints'][0x428014] for r in recs])
    i = np.argmax(e901 != 0)
    print('  901 seen at flip %d (t70 %.3f, previous %.3f); fade 0.005 (t70 - %.3f) clamped: max err %.2e' % (
        recs[i]['flip'], t70[i], t70[i - 1], t70[i - 1], np.abs(fd - np.clip(0.005 * (t70 - t70[i - 1]), 0, 1)).max()))
if part == 10:
    L = np.array([r['after'][0x427f70] for r in recs])
    ch = [i for i in range(1, len(L)) if L[i] != L[i - 1]]
    print('  light changes at flips', [(recs[i]['flip'], round(float(L[i]), 2)) for i in ch][:6], '... %d changes' % len(ch))
if part == 9:
    print('  902 switches (index after, event position, event t70, switch t70 = timer of the frame before, its dt):')
    for j, i in enumerate(sw):
        tk = recs[i]['tick']
        o, rw = ticks[tk][1], ticks[tk][2]
        print('    %d  %02x:%02x  %.3f  %.3f  dt %.3f' % (k[i], o, rw - rw % 8, ev[j], t70[i - 1], dts[i]))
