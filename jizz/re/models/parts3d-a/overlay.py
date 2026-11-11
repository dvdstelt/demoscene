"""Model of the 8-bit overlay chain used by both parts (all routines are core helpers, called with these
parameters by 0x17003 / 0x169e7):
  lines   0x1145c: value 0xf9, 4 px thick (x-major: rows y..y+3, y-major: columns x..x+3), dropped when an endpoint
          is outside 0 <= x < 320, 0 <= y < 200
  decay   (inline in the part): every byte >>= 3
  blur    0x11eb2 (ebp=0): p[i] = (p[i] + p[i+1] + p[i+320] + p[i+321]) >> 2, in place, rows 0..195 (0xf500 bytes)
  trail   0x12111 (cl = 2 or 1): q = (q << cl) + (p - q) >> cl in 8-bit arithmetic  (q: buffer + 0x10000)
  blend   0x11396: for every non-zero v: each channel c -> table[v][c] (lerp to white, tables of 0x1131b)
Checks with p1_hooks.pkl / p2_hooks.pkl."""
import sys, pickle, struct
import numpy as np

def decay(b):
    return (b >> 3).astype(np.uint8)

def blur(b):
    p = b.astype(np.int32).copy()
    n = 0xf500
    out = p.copy()
    # sequential in place, but reads only p[i], p[i+1], p[i+320], p[i+321] which are not yet written
    out[:n] = (p[:n] + p[1:n + 1] + p[320:n + 320] + p[321:n + 321]) & 0xff
    out[:n] >>= 2
    return out.astype(np.uint8)

def trail(src, dst, cl):
    s = src.astype(np.int32); d = dst.astype(np.int32)
    ah = ((d << cl) & 0xff) + ((s - d) & 0xff)
    return ((ah & 0xff) >> cl).astype(np.uint8)

def blend_tables(C=(31, 31, 31)):
    """0x1131b/0x11358: entry(v, c) = ((c*(31-v) + C*v) >> 5) for v, c in 0..31 (per 5-bit channel)."""
    v = np.arange(32)[:, None]; c = np.arange(32)[None, :]
    return [((c * (31 - v) + Ci * v) >> 5) for Ci in C]

def blend(page, ov):
    """page: uint16 565; ov: uint8 per pixel."""
    tr, tg, tb = blend_tables()
    p = page.astype(np.int32)
    r, g, b = (p >> 11) & 31, (p >> 5) & 63, p & 31
    v = ov.astype(np.int32)
    m = v != 0
    vv = np.minimum(v, 31)
    r2 = tr[vv, r]; b2 = tb[vv, b]
    g2 = tg[vv, g >> 1]          # guess: green handled as 5 bits (checked below)
    out = (r2 << 11) | (g2 << 6) | (b2)
    return np.where(m, out, p).astype(np.uint16)

if __name__ == '__main__':
    rec = pickle.load(open(sys.argv[1] if len(sys.argv) > 1 else 'p1_hooks.pkl', 'rb'))
    bufs = rec['buf']
    for i, e in enumerate(bufs):
        if e[0] != 'pre': continue
        _, tick, flips, raw, cls = e
        cl, use_blur = cls[0], cls[1]   # bytes [0x17485] (trail shift, 0 = none), [0x17486] (blur on)
        if not (i + 2 < len(bufs) and bufs[i + 1][0] == 'blend' and bufs[i + 2][0] == 'after'): continue
        A = np.frombuffer(raw[:64000], np.uint8); Q = np.frombuffer(raw[0x10000:0x10000 + 64000], np.uint8)
        a = decay(np.frombuffer(raw[:0x10000], np.uint8))
        if use_blur: a = blur(a)
        if cl:
            q = trail(a[:64000], Q, cl)
        else:
            q = a[:64000]
        _, _, _, esi, srcbuf, page = bufs[i + 1]
        srcbuf = np.frombuffer(srcbuf, np.uint8)
        after = np.frombuffer(bufs[i + 2][3], '<u2')
        page = np.frombuffer(page, '<u2')
        out = blend(page, srcbuf)
        print('tick', tick, 'flip', flips, 'cl', cl, 'blur', use_blur, 'overlay buffer exact:', (q == srcbuf).mean(),
              'blend exact: %.5f' % (out == after).mean(), 'max v', srcbuf.max())
