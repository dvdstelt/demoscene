"""Python model of the 3D parts 7, 8, 9, 10 of CLONE (what the parts compute before they call the 3D engine).

State recurrences per frame (dt = [0x420489]), the matrices, the deformation of the geosphere (parts 7, 8), the
metaball field, the voxel cubes, their vertex shades and texture coordinates (parts 9, 10), and the tile band.
Floats that the original stores as float are rounded with f32(); intermediates are float64 (the original: x87
80-bit, truncating rounding only for float -> int).
"""
import numpy as np

f32 = np.float32


def Rx(a):
    c, s = np.cos(a), np.sin(a); return np.array([[1, 0, 0], [0, c, -s], [0, s, c]])


def Ry(a):
    c, s = np.cos(a), np.sin(a); return np.array([[c, 0, s], [0, 1, 0], [-s, 0, c]])


def Rz(a):
    c, s = np.cos(a), np.sin(a); return np.array([[c, -s, 0], [s, c, 0], [0, 0, 1]])


def matrix(a, b, c, sy, t=(0, 0, 0)):
    """identity, 0x404f0f(a) = Rx, 0x404e36(b) = Ry, 0x404d5c(c) = Rz, 0x404cd6(1, sy, 1), 0x404feb(t): 3x4."""
    M = np.zeros((3, 4))
    M[:, :3] = np.diag([1, sy, 1]) @ Rz(c) @ Ry(b) @ Rx(a)
    M[:, 3] = t
    return M


def transform(M, v):
    return v @ M[:, :3].T + M[:, 3]


# ---- parts 7 and 8: the geosphere -------------------------------------------------------------------------------

def part7_step(s, dt):
    """s: dict angle [0xf61400], phase [0xf613f4], fade [0x427f80], n701 [0x427f74]. Returns the light [0x427f70]."""
    if s['n701'] < 1:
        s['angle'] = f32(s['angle'] + dt * 0.0075)
    if s['fade'] < 1.0:
        s['fade'] = f32(min(1.0, s['fade'] + dt * 0.0045))
    return 1.0 if s['fade'] >= 0.5 else f32(s['fade'] * 2)


def part7_deform(s, dt, orig, rot):
    """0x416e37: orig = geosphere vertices (float, from int16), rot = M orig. Returns the vertices given to the
    engine (radial scale around the object centre, then z += 2000)."""
    if s['n701'] < 1:
        s['phase'] = f32(s['phase'] + dt * f32(0.03))
    p = float(s['phase'])
    p8, p13 = float(f32(p * 0.8)), float(f32(p * 1.3))
    ix, iy, iz = (np.trunc(orig[:, k]).astype(np.int16).astype(float) for k in range(3))
    K = 112.0
    r = (0.5 * np.sin(ix / K + p) + 2 / 3) + (0.5 * np.sin(iy / K + p8) + 2 / 3) + (0.5 * np.sin(iz / K + p13) + 2 / 3)
    r = r.astype(f32).astype(float)
    k = np.minimum(f32(1 + (r - 2) * 3), 1.5)
    r = np.where(r > 2, f32(r * k), r)
    out = rot * r[:, None]
    out[:, 2] += 2000
    return out


def part8_step(s, dt):
    """s: tilet [0xf72d70], Z [0x427fe4], fade [0x427fec], angle [0xf72d6c], light [0x427f70]."""
    s['tilet'] = f32(s['tilet'] + dt * 0.05)
    if s['Z'] < 1400:
        s['fade'] = f32(max(0.0, s['fade'] - dt * 0.011))
    if s['Z'] > 1400:
        s['Z'] = f32(max(1400.0, s['Z'] - dt * 75))
    if s['Z'] >= 1400:
        s['light'] = f32(1400 / s['Z'])
    s['angle'] = f32(s['angle'] + dt * 0.0075)


def part8_deform(s, dt, orig, rot):
    """0x4176d0 (with [0x427fe8] = 0: event 802 is not in the table)."""
    s['phase'] = f32(s['phase'] + dt * f32(0.03))
    p = float(s['phase'])
    p8, p13 = float(f32(p * 0.8)), float(f32(p * 1.3))
    ix, iy, iz = (np.trunc(orig[:, k]).astype(np.int16).astype(float) for k in range(3))
    K = float(f32(69.11503601074219))
    r = (0.5 * np.sin(ix / K + p) + 2 / 3) + (0.5 * np.sin(iy / K + p8) + 2 / 3) + (0.5 * np.sin(iz / K + p13) + 2 / 3)
    r = r.astype(f32).astype(float)
    e802 = float(s.get('e802', 0.0))
    hi = np.minimum(f32((r - 3) * 2.4), 1.5) * e802 + 2.5
    hi = np.minimum(hi, 3.5)
    mid = r * np.minimum(f32(1 + (r - 2) * 0.1), 1.5)
    mid = np.minimum(mid, 2.5)
    r2 = np.where(r < 2, 2.0, np.where(r > 3, hi, np.where(r > 2, mid, r)))
    out = rot * r2[:, None]
    out[:, 2] += float(s['Z'])
    return out


# ---- parts 9 and 10: metaballs on a voxel grid -------------------------------------------------------------------

def wyvill(d2, R2):
    a = d2 / R2
    return ((-4 / 9 * a + 17 / 9) * a - 22 / 9) * a + 1           # = (1 - a)^2 (1 - 4a/9)


def field(cx, gx, gy, gz, R=(35, 35, 35)):
    """-135 + 270 sum of wyvill over the three blobs that are within reach (the reach test of the third blob uses
    R[1], its falloff R[2]; all three are 35 in CLONE)."""
    v = -135.0 if True else 0
    d = [(gx - cx[3 * i + 2]) ** 2 + (gy - cx[3 * i + 1]) ** 2 + (gz - cx[3 * i]) ** 2 for i in range(3)]
    out = np.zeros(np.broadcast(gx, gy, gz).shape)
    out += np.where(R[0] ** 2 > d[0], 270 * wyvill(d[0], R[0] ** 2) - 135, -135)
    out += np.where(R[1] ** 2 > d[1], 270 * wyvill(d[1], R[1] ** 2), 0)
    out += np.where(R[1] ** 2 > d[2], 270 * wyvill(d[2], R[2] ** 2), 0)
    return out


def centres9(t):
    """0x418094: blob centres (z, y, x for blob 0; then blob 1; blob 2) from the blob time t = [0xf735a0]."""
    t = float(t)
    k43, k30, k24 = float(f32(t * f32(1 / 43))), float(f32(t * f32(1 / 30))), float(f32(t * f32(1 / 24)))
    S = lambda arg: float(f32(8 * (np.sin(arg) * 2.5 + 4)))
    return [S(t * float(f32(1 / 23))), S(t * float(f32(1 / 33)) + 1), S(t * float(f32(1 / 43)) + 2),
            S(t * float(f32(1 / 30)) + 3), S(k43 + 4), S(t * float(f32(1 / 24)) + 5),
            S(k30 + 6), S(k43 + 7), S(k24 + 8)]


def grid9(t, fade):
    """G[z][y][x] (8x8x8 floats at 0xf72d74), cells 1..6 written; centre i of blob b: x = c[3b+2], y = c[3b+1],
    z = c[3b]."""
    c = centres9(t)
    G = np.zeros((8, 8, 8), f32)
    z, y, x = np.meshgrid(np.arange(1, 7) * 8.0, np.arange(1, 7) * 8.0, np.arange(1, 7) * 8.0, indexing='ij')
    G[1:7, 1:7, 1:7] = (field(c, x, y, z).astype(f32) * f32(fade)).astype(f32)
    return G


def centres10(t):
    """0x418fd2: c[0..8]; blob b: a-coordinate c[3b], b-coordinate c[3b+1], e-coordinate c[3b+2]."""
    t = float(t)
    k43, k30, k24 = float(f32(t * f32(1 / 43))), float(f32(t * f32(1 / 30))), float(f32(t * f32(1 / 24)))
    A = float(f32(2.3))
    S3 = lambda arg: float(f32(8 * (np.sin(arg) * A + 3)))
    S6 = lambda arg: float(f32(8 * (np.sin(arg) * 2 * A + 6)))
    return [S3(t * float(f32(1 / 23))), S6(t * float(f32(1 / 33)) + 1), S3(t * float(f32(1 / 43)) + 2),
            S3(k30 + 3), S6(k43 + 4), S3(t * float(f32(1 / 24)) + 5),
            S3(k30 + 6), S6(k43 + 7), S3(k24 + 8)]


def grid10(t):
    """The flat array g[k], k = 36 a + 6 b + e (a = 1..4, b = 1..10, e = 1..4), written in the loop order of
    0x418fd2 (e outer, b, a inner), so cells alias (a, b) <-> (a + 1, b - 6) and the last write wins."""
    c = centres10(t)
    g = np.zeros(256, f32)
    for e in range(1, 5):
        for b in range(1, 11):
            for a in range(1, 5):
                gx, gy, gz = 8.0 * a, 8.0 * b, 8.0 * e
                d0 = (gx - c[0]) ** 2 + (gy - c[1]) ** 2 + (gz - c[2]) ** 2
                d1 = (gx - c[3]) ** 2 + (gy - c[4]) ** 2 + (gz - c[5]) ** 2
                d2 = (gx - c[6]) ** 2 + (gy - c[7]) ** 2 + (gz - c[8]) ** 2
                v = -135.0
                if 35 * 35 > f32(d0): v = float(f32(270 * wyvill(float(f32(d0)), 1225.0) - 135))
                if 35 * 35 > f32(d1): v = float(f32(270 * wyvill(float(f32(d1)), 1225.0) + v))
                if 35 * 35 > f32(d2): v = float(f32(270 * wyvill(float(f32(d2)), 1225.0) + v))
                g[36 * a + 6 * b + e] = v
    return g


CUBE_FACES = None   # read from the exe: 0x428020, 12 x {a, b, c, mask bit}
CUBE_CORNERS = None  # 0x4280e0, 8 x float xyz (+-15)


def cube_mask(get, full=90.0):
    """0x418445 / 0x4193b6: neighbours (z-1, z+1, y-1, y+1, x-1, x+2!) -> bits 1, 2, 4, 8, 0x20, 0x10 cleared."""
    m = 0x3f
    for bit, nb in ((1, 'z-'), (2, 'z+'), (4, 'y-'), (8, 'y+'), (0x20, 'x-'), (0x10, 'x+2')):
        if get(nb) >= full:
            m &= ~bit
    return m


def cubes9(G):
    """List of (x, y, z, mask, size) in the call order of 0x418445."""
    out = []
    for z in range(1, 7):
        for y in range(1, 7):
            for x in range(1, 7):
                size = f32(G[z, y, x] * f32(1 / 6))
                if not size > 0.5: continue
                mask = 0x3f
                if size >= 15:
                    size = f32(15)
                    nb = {'z-': G[z - 1, y, x], 'z+': G[z + 1, y, x], 'y-': G[z, y - 1, x], 'y+': G[z, y + 1, x],
                          'x-': G[z, y, x - 1], 'x+2': G.reshape(-1)[z * 64 + y * 8 + x + 2]}
                    mask = cube_mask(nb.get)
                if mask:
                    out.append((30.0 * (x - 4), 30.0 * (y - 4), 30.0 * (z - 4), mask, size))
    return out


def cubes10(g, extra):
    """extra(k) = the float at flat index k >= 209 (program variables after the array, see notes)."""
    def G(k):
        return g[k] if k < 209 else extra(k)
    out = []
    for a in range(1, 5):
        for b in range(1, 11):
            for e in range(1, 5):
                k = 36 * a + 6 * b + e
                size = f32(g[k] * f32(1 / 6))
                if not size > 0.5: continue
                mask = 0x3f
                if size >= 15:
                    size = f32(15)
                    nb = {'z-': G(k - 36), 'z+': G(k + 36), 'y-': G(k - 6), 'y+': G(k + 6), 'x-': G(k - 1),
                          'x+2': G(k + 2)}
                    mask = cube_mask(nb.get)
                if mask:
                    out.append((30.0 * (e - 3), 30.0 * (b - 3), 30.0 * (a - 3), mask, size))
    return out


def build_cubes(cubes, phases, uvscale, faces, corners):
    """0x417c48 / 0x418b86: vertices (8 per cube), faces (vertex indices), per-vertex shade (16.16), uv."""
    V, F, SH, UV = [], [], [], []
    ph = [float(p) for p in phases]
    for (x, y, z, mask, s) in cubes:
        n = len(V)
        xs, xp, ys, yp, zs, zp = (float(f32(x - s)), float(f32(x + s)), float(f32(y - s)), float(f32(y + s)),
                                  float(f32(z - s)), float(f32(z + s)))
        V += [(xs, ys, zs), (xp, ys, zs), (xp, yp, zs), (xs, yp, zs), (xs, ys, zp), (xp, ys, zp), (xp, yp, zp),
              (xs, yp, zp)]
        for (i0, i1, i2, bit) in faces:
            if not mask & bit: continue
            F.append((n + i0, n + i1, n + i2))
            sh, uv = [], []
            for i in (i0, i1, i2):
                vx, vy, vz = V[n + i]
                v = 2031616 * (np.sin(vx * 0.035 + ph[0]) + np.sin(vy * 0.035 + ph[1]) + np.sin(vz * 0.035 + ph[2])) \
                    + 12582912
                sh.append(int(np.trunc(v)))
                cx, cy, cz = corners[i]
                uv += [float(f32(uvscale * (x + cx) + uvscale * (z + cz))), float(f32(uvscale * (y + cy) - uvscale * (z + cz)))]
            SH.append(sh); UV.append(uv)
    return np.array(V), np.array(F), np.array(SH), np.array(UV)


# ---- the tile band ------------------------------------------------------------------------------------------------

def tiles(T, part, s=None):
    """320x200 RGB of what the part's tile loop writes (rows 4..195), None where it does not write.
    T = the 192 dwords at 0x428164. part 7: s = fade [0x427f80], n701; part 8: tilet, fade, columns."""
    img = np.zeros((200, 320, 3), np.int64)
    written = np.zeros((200, 320), bool)
    idx = 0 if part in (7, 9) else 24
    for row in range(-1, 7):
        for col in range(8):
            t = T[idx:idx + 3]
            if part == 7:
                v = [int(np.trunc(float(f32(0.0125)) * x * 255 * float(s['fade']))) for x in t]
                vis = 1 <= col <= 5 and 0 <= row <= 5 and s['n701'] < 2
            elif part == 8:
                w = f32(np.sin(t[0] * float(s['tilet']) * float(f32(0.01)) + idx * 0.2) * 0.1 + 0.9)
                w = min(float(w), 1.0)
                v = [int(np.trunc(float(f32(0.0125)) * x * 255 * float(s['fade']) * w)) for x in t]
                vis = 1 <= row <= 4 and col < s['cols']
            elif part == 9:
                v = [int(np.trunc(float(f32(0.0125)) * x * 255)) for x in t]
                vis = 2 <= row <= 3
            else:
                v = [int(np.trunc(float(f32(0.0125)) * x * 255)) for x in t]
                vis = 2 <= row <= 5
            v = [min(x, 255) for x in v]
            y0, x0 = 24 * row + 28, 40 * col
            img[y0:y0 + 24, x0:x0 + 40] = v if vis else 0
            written[y0:y0 + 24, x0:x0 + 40] = True
            idx += 3
    if part in (7, 10):
        img[0:4] = 0; written[0:4] = True
    if part == 7:
        img[196:200] = 0; written[196:200] = True
    return img, written
