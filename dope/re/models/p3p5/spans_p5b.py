"""Count env spans longer than 255 pixels (truncated by the 8-bit counter) in chosen p5 frames, and how many
pixels the right-edge clip (x = 319 never drawn) leaves out."""
import sys, os
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import p5model
d = np.load(sys.argv[1])
frames = [int(x) for x in sys.argv[2].split(',')]
long_spans = []
orig_rows = p5model.EnvFiller.rows
def span_hook(self, x0, cnt, A0, B0, hA, hB):
    return p5model.em.Filler.span(self, x0, cnt, A0, B0, hA, hB)
import engine_model as em
class Probe(p5model.EnvFiller):
    pass
orig_span = p5model.EnvFiller.span
def rows(self, y0, n, sl, sr, XL, XR, ecx, st, clip):
    # replicate the count logic to log raw counts
    M32 = em.M32
    XL2, XR2 = XL, XR
    base = y0 * 320
    sl2 = (sl + 0x1400000) & M32; sr2 = (sr + 0x1400000) & M32
    for r in range(n):
        left = XL2 >> 16; right = XR2 >> 16
        if clip:
            l = max(left, base); rr = min(max(right, base), base + 319)
            raw = rr - l
            base += 320
        else:
            raw = em.s16(right - left)
        if raw > 255:
            long_spans.append(raw)
        ebp = ((XR2 & 0xffff) << 16) | (XL2 >> 16); edx = ((XL2 & 0xffff) << 16) | (XR2 >> 16)
        esi = ((sr2 & 0xffff) << 16) | (sl2 >> 16); edi = ((sl2 & 0xffff) << 16) | (sr2 >> 16)
        t = ebp + esi; cf = t >> 32; ebp = t & M32
        t = edx + edi + cf; cf = t >> 32; edx = t & M32
        ebp = (ebp + cf) & M32
        XL2 = ((ebp & 0xffff) << 16) | (edx >> 16); XR2 = ((edx & 0xffff) << 16) | (ebp >> 16)
    return orig_rows(self, y0, n, sl, sr, XL, XR, ecx, st, clip)
p5model.EnvFiller.rows = rows
bg = p5model.background()
for f in frames:
    k = int(np.where(d['frame'] == f)[0][0])
    long_spans.clear()
    p5model.render_frame(d['o0'][k], d['o1'][k], -1, 0x248f, d['tab'][k].astype(np.int64), bg)
    print('frame', f, 'spans > 255 px:', len(long_spans), sorted(long_spans)[-5:])
