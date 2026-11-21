"""Check the scene geometry: object -> world -> screen for the obuke and the fish vertices the rasterizers get
(run1.pkl face dumps: vertex +0x14 / +0x18 = screen x, y in 1/16 pixel, +0x1c = camera z x 128), and print the
size of both objects."""
import math, pickle, struct, sys
import numpy as np
sys.path.insert(0, '../..')
import datfile

d = datfile.Dat('../../../original/INSIDE.DAT')


def sro(name, scale):
    b = d.get(name)
    _, voff, nv, foff, nf = struct.unpack('<5i', b[:20])
    V = [struct.unpack('<hhh', b[voff + 12 * i:voff + 12 * i + 6]) for i in range(nv)]
    Fc = [struct.unpack('<4H', b[foff + 8 * i:foff + 8 * i + 8]) for i in range(nf)]
    s = scale / 65536
    return np.array([[math.trunc(c * s) for c in v] for v in V]), Fc, nv, nf


for name, sc in (('water/obuke.sro', 0x9999), ('water/fish.sro', 0x2666)):
    V, Fc, nv, nf = sro(name, sc)
    print('%s: %d vertices, %d faces, scaled by %#x/65536: x %d..%d, y %d..%d, z %d..%d; face word 0 values %s' % (
        name, nv, nf, sc, V[:, 0].min(), V[:, 0].max(), V[:, 1].min(), V[:, 1].max(), V[:, 2].min(), V[:, 2].max(),
        sorted(set(f[0] for f in Fc))))

D = pickle.load(open('run1.pkl', 'rb'))
for k in (1, 2):
    f, g = D['frames'][k], D['frames'][k + 1]
    err = {'obuke': 0.0, 'fish': 0.0}
    for name, addr, face, vs, uv in f['faces']:
        for v in vs:
            if name == 'obuke':
                M = np.array(f['obj_mat']).reshape(3, 3)
                p = M @ np.array(v[2:5], float) + [-600, 0, 0]
            else:
                best = None
                for i in range(8):
                    Mi = np.array(g['fish'][i]['mat']).reshape(3, 3)
                    q = Mi @ np.array(v[2:5], float) + np.array(g['fish'][i]['pos'], float)
                    e = abs(q[2] + 1600 - v[7] / 128)
                    if best is None or e < best[0]:
                        best = (e, q)
                p = best[1]
            z = p[2] + 1600
            sx, sy = 160 + 256 * p[0] / z, 100 + 27306 / 16384 * 128 * p[1] / z
            err[name] = max(err[name], abs(sx - v[5] / 16), abs(sy - v[6] / 16), abs(z - v[7] / 128))
    print('frame %d: projected vertices vs the engine (pixels / depth units): max diff %s' % (
        k, {a: round(b, 3) for a, b in err.items()}))
