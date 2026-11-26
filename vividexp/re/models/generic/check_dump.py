"""Statistics of a --gen-dump run and the capture: frames drawn per refresh, which draw each captured refresh shows,
the lag between the state's time and the refresh that shows it; and model-vs-emulator page comparisons.

usage: python3 check_dump.py runs/landall 5951 gen/gen.ini [every]
       (5951 = the part2 capture frame of the snapshot the run started from = GenericEffect's start)
"""
import sys
import numpy as np

sys.path.insert(0, '../..')
from sheet import open_frames, indexed
import render
from gen import idiv

prefix, start, ini = sys.argv[1], int(sys.argv[2]), sys.argv[3]
every = int(sys.argv[4]) if len(sys.argv) > 4 else 0
draws, shows = [], []
for line in open(prefix + '.gentxt'):
    f = line.split()
    if f[0] == 'draw':
        draws.append((int(f[1]), int(f[3]), int(f[7]), f[9], [[int(x) for x in s.split(':')[1].split(',')] for s in f[10:]]))
    else:
        shows.append((int(f[2]), int(f[8], 16), int(f[10])))
sc = render.Scene(ini)
o0 = sc.objs[0]
t_of = lambda d: sc.t_from_pos(o0, d[4][0][0])
print('draws', len(draws), 'shows', len(shows), 'refreshes', shows[-1][0] - start)
# page shown at each show: the latest draw into that page before the show
pages = np.memmap(prefix + '.pages', np.uint8, 'r')
shown = []                                    # (refresh, draw index)
for r, page, mi in shows:
    cand = [d for d in draws if d[3] == '%x' % page and d[1] <= r]
    shown.append((r, cand[-1][0] if cand else None))
lags = {}
for r, k in shown:
    if k is not None:
        lag = r - start - t_of(draws[k])
        lags[lag] = lags.get(lag, 0) + 1
print('lag (refresh - start - t of the shown state):', dict(sorted(lags.items())))
gaps = np.diff([r for r, _ in shown])
print('refreshes between shown frames:', dict(zip(*np.unique(gaps, return_counts=True))))
# the capture: refresh R shows the page of the last show <= R  (record R is taken after refresh R's retrace)
raw, n = open_frames('../../part2.frames')
bad = 0
checked = 0
si = 0
for R in range(shown[0][0], shown[-1][0] + 1, 7):
    while si + 1 < len(shown) and shown[si + 1][0] <= R:
        si += 1
    k = shown[si][1]
    px, _ = indexed(raw, R)
    ref = np.asarray(pages[k * 64000:(k + 1) * 64000]).reshape(200, 320)
    checked += 1
    if not (px == ref).all():
        bad += 1
        if bad < 5:
            print('capture %d != draw %d (%d px)' % (R, k, (px != ref).sum()))
print('capture refreshes checked %d, not equal to the expected draw: %d' % (checked, bad))
if every:
    tot = 0
    for k in range(0, len(draws), every):
        d = draws[k]
        ts = [sc.t_from_pos(o, d[4][i][0]) for i, o in enumerate(sc.objs)]
        page = render.render_frame(sc, ts)
        ref = np.asarray(pages[k * 64000:(k + 1) * 64000]).reshape(200, 320)
        if k == 1 and sc.bgpic is not None:
            print('draw 1: page was zero-filled, not the background picture')
            page = render.render_frame(sc, ts, page=np.zeros((200, 320), np.int64))
        nd = int((page != ref).sum())
        tot += nd
        print('draw %d t %d: model differs in %d pixels' % (k, ts[0], nd))
    print('total differing pixels', tot)
