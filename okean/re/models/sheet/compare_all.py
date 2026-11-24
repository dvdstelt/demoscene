"""Run the sheet model (sheet_model.py) over the whole part and compare every frame with full.frames.

usage: python3 compare_all.py [--show REC[,REC..]]   (--show: model | capture | difference as show_<REC>.png)

Schedule (what the capture shows; see notes-sheet.md "Which frame shows what"):
- refreshes 6893..6895: set-up (black screen); the timer's first callback runs at the retrace of refresh 6896, so
  callback n runs in refresh 6895 + n;
- draw j starts after the copy of draw j - 1 in refresh P (after callback P - 6895, which ran at that retrace) and
  uses the state after c = P - 6895 callbacks (draw 0: c = 0, started in refresh 6895); its picture is copied after
  the next retrace the main loop sees;
- a refresh whose picture equals the one before is a refresh in which the main loop missed the retrace (the GUS
  interrupt, which waits inside its handler for a volume-ramp interrupt, held it across the 2-line retrace): the
  draw is copied one refresh later and the next draw has skipped a callback (case a). Once (refresh 7516) the
  interrupt held the main loop between the retrace's start and end until after the refresh ended: that draw was
  copied early in refresh 7517 and a second draw (with one callback more) was made and copied in the same refresh,
  so the first is never seen in the capture (case b, tried when case a does not match);
- the palette is written at the end of each draw, before the copy: a record shows the palette of the draw it
  shows or of the next draw (written before the record's end);
- order 27 is first seen by callback 1138 (refresh 8033); the printer and the main loop's palette choice take the
  position of full.ticks at the end of refresh P.
"""
import copy
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from sheet_model import RE, Tables, State, Printer, draw  # noqa: E402
sys.path.insert(0, RE)
from sheet import open_frames  # noqa: E402

T = Tables()
raw, W, H, rec, hdr, nfr = open_frames(os.path.join(RE, 'full.frames'))


def cap(k):
    o = hdr + k * rec
    pal = np.frombuffer(bytes(raw[o + 8:o + 776]), np.uint8)
    idx = np.asarray(raw[o + 776:o + 776 + W * H]).reshape(H, W)[:200, :320]
    return idx.reshape(-1), pal


ticks = {}
for ln in open(os.path.join(RE, 'full.ticks')):
    if ln[0] != '#':
        f = ln.split()
        ticks[int(f[0])] = (int(f[2]), int(f[3]))
K0 = 6896           # first refresh with a drawn picture
FIRST27 = 1138      # first callback that sees order 27
KEND = 8096         # last refresh whose picture the part copied
show = set(int(v) for v in sys.argv[sys.argv.index('--show') + 1].split(',')) if '--show' in sys.argv else set()


def one(c, P, seed, pr):
    order, row = ticks[P]
    st = State(T, c, max(0, c - FIRST27 + 1))
    img, pal, seed = draw(T, st, seed, pr, order, row)
    return st, img, pal, seed


def save_show(k, img, pal, ci):
    from PIL import Image
    cp = cap(k)[1]
    a = (pal.reshape(256, 3).astype(int) * 255 // 63).astype(np.uint8)[img.reshape(200, 320)]
    b = (cp.reshape(256, 3).astype(int) * 255 // 63).astype(np.uint8)[ci.reshape(200, 320)]
    d = np.zeros_like(a)
    d[(img != ci).reshape(200, 320)] = (255, 0, 0)
    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'show_%d.png' % k)
    Image.fromarray(np.concatenate([a, b, d], 1)).resize((1920, 400), Image.NEAREST).save(out)
    print('wrote', out)


seed = T.seed0
pr = Printer(T)
P = K0 - 1
shown = []          # (record, draw index, callbacks, image matches)
pals = []           # palette of every draw, in order
repeats, hidden = [], []
for k in range(K0, KEND + 1):
    ci, _ = cap(k)
    if k > K0 and (ci == cap(k - 1)[0]).all():
        repeats.append(k)
        continue
    c = P - (K0 - 1)
    pr_a = copy.copy(pr)
    st, img, pal, seed_a = one(c, P, seed, pr_a)
    npx = int((ci != img).sum())
    if npx and repeats and repeats[-1] == k - 1 and P == k - 2:
        pr_b = copy.copy(pr)
        _, _, pal_h, seed_b = one(c, P, seed, pr_b)                 # the hidden draw
        st2, img2, pal2, seed_b2 = one(c + 1, k - 1, seed_b, pr_b)
        npx2 = int((ci != img2).sum())
        if npx2 < npx:
            hidden.append(k)
            pals.append(pal_h)
            st, img, pal, npx, seed, pr, c = st2, img2, pal2, npx2, seed_b2, pr_b, c + 1
        else:
            seed, pr = seed_a, pr_a
    else:
        seed, pr = seed_a, pr_a
    pals.append(pal)
    if k in show:
        save_show(k, img, pal, ci)
    shown.append((k, len(pals) - 1, c, npx, st.b))
    P = k

bad_px = [s for s in shown if s[3]]
bad_pal = []
for k, j, c, npx, b in shown:
    cp = cap(k)[1]
    ok = (cp == pals[j]).all() or (j + 1 < len(pals) and (cp == pals[j + 1]).all())
    if not ok:
        bad_pal.append((k, j, c, b))
for s in bad_px[:40]:
    print('record %d draw %d c %d: %d pixels differ' % s[:4])
for s in bad_pal[:40]:
    print('record %d draw %d c %d b %d: palette is neither this draw\'s nor the next one\'s' % s)
print('records %d..%d: %d drawn pictures (%d draws), %d with differing pixels, %d with a palette not explained' % (
    K0, KEND, len(shown), len(pals), len(bad_px), len(bad_pal)))
print('refreshes repeating the picture before (missed retrace):', repeats)
print('refreshes after a hidden draw (case b):', hidden)
