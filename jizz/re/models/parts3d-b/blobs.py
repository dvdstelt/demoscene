"""Model of Nix's blobs (part 0x10fda): ball paths 0x7aae, field 0x7a4e (with 0x662f and the tables 0x6716 /
0x7b9e), surface-following marching cubes 0x77ad / 0x77d5 / 0x6e74 with the case table built by 0x6cdd from
0x28ed8, edge vertices 0x7144.., normals 0x79a8. Integer parts are exact; floats in float32.

usage: python3 blobs.py DUMP.pkl     compare with a memory dump taken by run_jizz_b.py --dump-at 0x11130:...
       (at 0x11130 the frame has been built and rendered: obj 1 = blob)
"""
import sys, os, pickle, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
import numpy as np
import rep
sys.setrecursionlimit(100000)
f32 = np.float32

# --- static data / tables ------------------------------------------------------------------------------
K1 = [struct.unpack_from('<i', rep.SEG, 0x78a0 + 4 * i)[0] for i in range(6)]   # angle speeds (static in the exe)
K2 = [struct.unpack_from('<i', rep.SEG, 0x78d0 + 4 * i)[0] for i in range(6)]
K3 = [struct.unpack_from('<i', rep.SEG, 0x7900 + 4 * i)[0] for i in range(6)]
SIN = [int(round(1024 * np.sin(np.float32(i * 16) * np.float32(0.000383495197)))) for i in range(1280)]  # 0x10fa2 (+-1, use mem)
RSQ = [4096] + [int(np.rint(np.float32(4096) / np.float32(np.sqrt(np.float32(i))))) for i in range(1, 256)]  # 0x6b16 -> 0x6716
TRI = []                                                    # 0x28ed8: 256 x 8 bytes -> edge lists (0x6cdd)
for c in range(256):
    rec = rep.SEG[0x28ed8 + 8 * c: 0x28ed8 + 8 * c + 8]
    n = rec[0] & 7
    nib = [(rec[k // 2] >> (4 * (k % 2))) & 15 for k in range(1, 1 + 3 * n)]
    TRI.append([nib[3 * i:3 * i + 3] for i in range(n)])
# edge e: (corner a offset, axis); corner b = a + unit(axis)
EDGES = [((0, 0, 0), 0), ((0, 0, 0), 1), ((1, 0, 0), 1), ((0, 1, 0), 0), ((0, 0, 0), 2), ((1, 0, 0), 2),
         ((0, 1, 0), 2), ((1, 1, 0), 2), ((0, 0, 1), 0), ((0, 0, 1), 1), ((1, 0, 1), 1), ((0, 1, 1), 0)]
LO, HI, STEP = -0x400, 0x380, 0x40


def g(d2):
    """0x662f: ~65536/sqrt(d2) from the 256-entry table, with the argument normalised by powers of 4"""
    if d2 >= 0x400000: return RSQ[d2 >> 16] >> 4
    if d2 >= 0x100000: return RSQ[d2 >> 14] >> 3
    if d2 >= 0x40000: return RSQ[d2 >> 12] >> 2
    if d2 >= 0x10000: return RSQ[d2 >> 10] >> 1
    if d2 >= 0x4000: return RSQ[d2 >> 8]
    if d2 >= 0x1000: return RSQ[d2 >> 6] << 1
    if d2 >= 0x400: return RSQ[d2 >> 4] << 2
    if d2 >= 0x100: return RSQ[d2 >> 2] << 3
    if d2 >= 0x40: return RSQ[d2] << 4
    if d2 >= 0x10: return RSQ[d2 << 2] << 5
    if d2 >= 4: return RSQ[d2 << 4] << 6
    return RSQ[d2 << 6] << 7


def balls(T, sin=SIN):
    """0x7aae: T = xmpGetTimer + [0x6542] (PIT clocks since the part start)"""
    t = (T * 710000) >> 32                       # mul 0xad570, edx
    P = []
    for i in range(6):
        a = ((K1[i] * t * 3) >> 5) & 0x3ff
        b = ((K2[i] * t * 3) >> 5) & 0x3ff
        c = ((K3[i] * t * 3) >> 5) & 0x3ff
        r = (sin[c] >> 2) + 490
        x = (sin[a] * r) >> 10
        y = (sin[a + 256] * r) >> 11
        z = sin[b] >> 1
        P.append((x, y, z))
    return P


def field(P, x, y, z):
    """0x7a4e: field byte at grid point (x, y, z); inside when >= 128"""
    s = 0
    for (bx, by, bz) in P:
        s += 21 * g((bx - x) ** 2 + (by - y) ** 2 + (bz - z) ** 2)
    return min(255, s >> 7)


def normal(P, p):
    """0x79a8: n = sum (p - c)/|p - c|^3, scaled to length 2^23 (float32)"""
    a = np.zeros(3, np.float32)
    for c in P:
        d = (p - np.array(c, np.float32)).astype(np.float32)
        d2 = f32(np.sum(d * d)); w = f32(1) / (d2 * f32(np.sqrt(d2)))
        a = a + d * w
    s = f32(8388608.0) / f32(np.sqrt(np.sum(a * a)))
    return a * s


def build(P):
    """0x77ad: returns vertices (float32 xyz), normals, faces (vertex index triples) in the original's order"""
    fcache, visited, ecache = {}, set(), {}
    verts, faces = [], []

    def F(x, y, z):
        k = (x, y, z)
        if k not in fcache: fcache[k] = field(P, x, y, z)
        return fcache[k]

    def edge(cx, cy, cz, e):
        (ox, oy, oz), ax = EDGES[e]
        a = (cx + ox * STEP, cy + oy * STEP, cz + oz * STEP)
        key = (a, ax)
        if key in ecache: return ecache[key]
        b = list(a); b[ax] += STEP
        fa, fb = F(*a), F(*b)
        num, den = (128 - fa) << 6, fb - fa
        off = int(num / den)                          # idiv: truncate toward zero
        p = list(a); p[ax] += off
        verts.append(np.array(p, np.float32))
        ecache[key] = len(verts) - 1
        return ecache[key]

    def cell(x, y, z):
        for dx, dy, dz in ((0, 0, 0), (1, 0, 0), (1, 1, 0), (1, 1, 1), (0, 1, 1), (0, 1, 0), (1, 0, 1), (0, 0, 1)):
            F(x + dx * STEP, y + dy * STEP, z + dz * STEP)
        visited.add((x, y, z))
        case = 0
        for bit, (dx, dy, dz) in enumerate(((0, 0, 0), (1, 0, 0), (0, 1, 0), (1, 1, 0), (0, 0, 1), (1, 0, 1), (0, 1, 1), (1, 1, 1))):
            if F(x + dx * STEP, y + dy * STEP, z + dz * STEP) & 0x80: case |= 1 << bit
        for e0, e1, e2 in TRI[case]:
            v0 = edge(x, y, z, e0); v1 = edge(x, y, z, e1); v2 = edge(x, y, z, e2)
            faces.append((v2, v1, v0))                 # [ebp+8] = first edge, [ebp+4] = second, [ebp] = third
        for mask, (dx, dy, dz) in ((0xf0, (0, 0, 1)), (0x0f, (0, 0, -1)), (0x33, (0, -1, 0)), (0xcc, (0, 1, 0)),
                                   (0x55, (-1, 0, 0)), (0xaa, (1, 0, 0))):
            m = case & mask
            if m == 0 or m == mask: continue
            nx, ny, nz = x + dx * STEP, y + dy * STEP, z + dz * STEP
            if dx < 0 and x <= LO or dy < 0 and y <= LO or dz < 0 and z <= LO: continue
            if dx > 0 and x >= HI or dy > 0 and y >= HI or dz > 0 and z >= HI: continue
            if (nx, ny, nz) in visited: continue
            cell(nx, ny, nz)

    for i in range(5, -1, -1):                        # 0x77ad: balls 5..0
        bx, by, bz = (c & ~63 for c in P[i])
        if not (LO < bx < HI and LO < by < HI and LO < bz < HI): continue
        x = bx; found = None
        while True:                                   # march +x while inside
            x += STEP
            if x >= HI: break
            if not (F(x, by, bz) & 0x80): found = x - STEP; break
        if found is None:                             # hit the edge: march -x
            x = bx
            while True:
                x -= STEP
                if x <= LO: break
                if not (F(x, by, bz) & 0x80): found = x; break
        if found is None: continue
        cell(found, by, bz)                           # no visited check here: a seed cell on an already traced
                                                      # surface is polygonised again (duplicate faces)
    V = np.array(verts, np.float32)
    N = np.array([normal(P, v) for v in V], np.float32) if len(V) else np.zeros((0, 3), np.float32)
    return V, N, faces


if __name__ == '__main__':
    d = pickle.load(open(sys.argv[1], 'rb')); m = d['mem']
    rd = lambda a: struct.unpack_from('<I', m, a)[0]; ri = lambda a: struct.unpack_from('<i', m, a)[0]
    sin = [ri(0xfb9e + 4 * i) for i in range(1280)]
    T = (ri(0x6542) + 0) & 0xffffffff
    Pmem = [(ri(0x7918 + 4 * i), ri(0x7930 + 4 * i), ri(0x7948 + 4 * i)) for i in range(6)]
    tprime = [ri(0x7888 + 4 * i) for i in range(6)]
    Tf = struct.unpack_from('<f', m, 0x159f7)[0]; ts = struct.unpack_from('<f', m, 0x1579c)[0]
    Tpit = round(Tf / ts)                            # T used for the camera (same frame, read again later)
    O = rd(0x18d50) + 0x50
    f0, nf, v0, nv = rd(O), rd(O + 4), rd(O + 8), rd(O + 12)
    VB, FB = rd(0x18d4c), rd(0x18d48)
    raw = np.frombuffer(m[VB + 0x40 * v0: VB + 0x40 * (v0 + nv)], np.float32).reshape(nv, 16)
    fr = np.frombuffer(m[FB + 0x1c * f0: FB + 0x1c * (f0 + nf)], np.uint8).reshape(nf, 0x1c)
    fidx = (fr[:, 0:12].copy().view('<u4').reshape(nf, 3).astype(np.int64) - VB) // 0x40 - v0
    print('dump tick', d['tick'], 'camera T', Tf, '~PIT', Tpit, 'balls (mem)', Pmem)
    # find the PIT time that gives these ball positions near Tpit (the ball T is read a little earlier)
    best = None
    for dT in range(-40000, 1, 50):
        if balls(Tpit + dT, sin) == Pmem: best = Tpit + dT; break
    print('ball positions reproduced from T =', best, '(camera T - %s)' % (None if best is None else Tpit - best))
    V, N, Fc = build(Pmem)
    print('model: %d verts %d faces; emulator: %d verts %d faces; tex %s mode %s flags %s' % (
        len(V), len(Fc), nv, nf, set(fr[:, 0xe]), set(fr[:, 0xd]), set(fr[:, 0xc])))
    if len(V) == nv:
        print('max |dpos|', np.abs(V - raw[:, 0:3]).max(), ' max |dn|/2^23', np.abs(N - raw[:, 8:11]).max() / 8388608,
              ' uv == n.xy:', np.abs(raw[:, 11:13] - raw[:, 8:10]).max(), ' flag', set(raw[:, 15].view(np.uint32) & 0xff))
    if len(Fc) == nf: print('faces equal:', np.array_equal(np.array(Fc), fidx))
