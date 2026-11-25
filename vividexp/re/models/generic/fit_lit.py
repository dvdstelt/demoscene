"""How texture.lit (lit[shade][texel], 128 shades used) relates to the palette: fit colour(lit[s][t]) ~ f(colour(t), s)."""
import sys
import numpy as np
import gen

d = sys.argv[1] if len(sys.argv) > 1 else 'gen2'
pal = np.frombuffer(gen.load(d + '/texture.pal'), np.uint8).reshape(256, 3).astype(float)
lit = np.frombuffer(gen.load(d + '/texture.lit')[:32768], np.uint8).reshape(128, 256)
tex, _ = gen.pcx(d + '/texture.pcx')
used = np.unique(tex)
print('texels used', used.min(), used.max(), len(used))
print('lit values range for used texels', lit[:, used].min(), lit[:, used].max())
print('lit for unused texels (t<64): row 64', lit[64, :8], 'row 127', lit[127, :8])
# per shade: average ratio of output to texel colour
for s in (0, 8, 16, 32, 48, 63, 64, 72, 80, 96, 112, 120, 127):
    c = pal[lit[s, used]]
    t = pal[used]
    print('s %3d  out mean %s  tex mean %s  ratio %s  out-ratio*t?' % (s, c.mean(0).round(1), t.mean(0).round(1),
                                                              (c.sum(0) / t.sum(0)).round(3)))
# candidate models: out = clamp(t * k(s) + w(s)), k, w per shade by least squares on all used texels / channels
print('least squares out = t*k + w*63 per shade:')
for s in range(0, 128, 8):
    c = pal[lit[s, used]].ravel()
    t = pal[used].ravel()
    A = np.stack([t, np.full_like(t, 63.0)], 1)
    (k, w), res, *_ = np.linalg.lstsq(A, c, rcond=None)
    err = np.abs(A @ [k, w] - c).max()
    print('  s %3d k %.3f w %.3f maxerr %.1f' % (s, k, w, err))
