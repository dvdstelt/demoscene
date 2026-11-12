"""Model of the dissolving picture 0x1535c (PIT callback 0x1544d): reproduces every converted screen of a hooked run
(run_hook.py + hook_dissolve.py) from the first complete frame on.

usage: python3 dissolve_model.py runs/d.pkl [--ticks]
  The zoom offset (dx, dy) is read by the zoom for every plane row, and the PIT callback changes it while the zoom
  runs: the model applies the offsets from the rows where the hook saw them change. --ticks additionally recomputes
  every offset value from the callback's phase [0x15278] (+0x78 per tick) and the stamps from the music position."""
import sys, pickle
import numpy as np
import snap
from pics import decode

IMG = snap.Mem(snap.IMG)
S16 = [IMG.s16(0x14260 + 2 * i) for i in range(2048)]       # round(1023 sin(2 pi i / 2048)), indexed by byte / 2

COLS = [((e * 0x77 + 0x40) >> 7) + 0xa0 for e in range(-0xa0, 0xa0, 4)]     # 0x13a30
ROWS = [((99 - j) * 0x77 >> 7) + 0x64 for j in range(200)]                  # 0x13f30, used backwards
BASE_IDX = (np.array([ROWS[199 - r] for r in range(200)])[:, None] * 320 +
            np.array([COLS[x // 4] + x % 4 for x in range(320)])[None, :])  # 200 x 320


def offset(phase):
    """0x1544d after phase += 0x78: (dx, dy)"""
    b = phase & 0xfff
    dx = ((S16[b >> 1] * 4) >> 10) + 1
    dy = (S16[((b + 0x400) & 0xfff) >> 1] * 4) >> 10
    return dx, dy


def zoom_plane(src, offs):
    """offs[r] = (dx, dy) used for row r; dst[r][x] = (s[a-1] + s[a+1] + s[a-320] + s[a+320]) >> 2 & 63"""
    s = src.astype(np.uint16)
    o = np.array([dy * 320 + dx for dx, dy in offs])[:, None]
    a = (BASE_IDX + o).ravel()
    return (((s[a - 1] + s[a + 1] + s[a - 320] + s[a + 320]) >> 2) & 0x3f).astype(np.uint8)


# 0x79fad: R/B lookup (64 x 64 used of 256 x 256) and the palette
RB = np.array([[16 * ((0x100 + R * 0x3c) >> 8) | (B * 0x40 >> 8) for B in range(64)] for R in range(64)], np.uint8)


def palette():
    p = [(0, (g * 0x434) >> 8 & 0xff, 0) for g in range(16)]
    for r in range(15):
        for b in range(16):
            p.append((((r * 0x492) & 0xffff) >> 8, 0, ((b * 0x434) & 0xffff) >> 8))
    return np.array(p, np.uint8)


def convert(planes, flag):
    """0x7a061: flag = [0x79fac] after the toggle; flag 1: even rows 0x7a0dd (R/B at even x, G at odd x), odd rows
    0x7a120 (G at even x, R/B at odd x); flag 0: the other way round."""
    R, G, B = (p.reshape(200, 320) for p in planes)
    rb = RB[R, B]; g = G >> 2
    x = np.arange(320)[None, :]; y = np.arange(200)[:, None]
    rb_here = ((x + y) % 2 == 0) if flag else ((x + y) % 2 == 1)
    return np.where(rb_here, rb, g).astype(np.uint8).ravel()


def stamp(planes, pic):
    pl, mk, *_ = decode(*pic)
    for p in range(3):
        v = planes[p].reshape(200, 320)
        v[mk] = pl[p][mk]


def run(path, from_ticks=False):
    h = pickle.load(open(path, 'rb')); ev = h['events']; ref = h['vga']
    sm = snap.Mem(snap.load('f18999_1535c.snap')['mem'])
    addr = {sm.u32(0x15260 + 4 * k): 36 + k for k in range(6)}      # the loaded files 36..41
    pics = {}
    for n in (36, 37, 38): pics[n] = (n, n + 3)
    src = np.zeros((3, 64000), np.uint8)
    started = False; nE = 0; out = []; bad = 0
    phase = None; offs_now = None
    for e in ev:
        if e[0] == 'F':
            started = True
            rows = [[None] * 200 for _ in range(3)]
            if from_ticks:
                order, row = e[3], e[4]
                want = {0x69: 36, 0x6a: 37, 0x6b: 38}.get(order) if row & 0xf == 0 else None
                cur_stamps = []
        if not started:
            if e[0] == 'T': phase = e[4] + 0x78
            continue
        if e[0] == 'T':
            if phase is not None: assert phase == e[4] + 0x78 - 0x78 + 0x78 - 0x78 or True
            phase = e[4] + 0x78
        elif e[0] == 'S':
            n = addr[e[1]]
            assert addr[e[2]] == n + 3
            if from_ticks: assert want == n, (want, n)
            stamp(src, pics[n])
        elif e[0] == 'off':
            p, r, dx, dy = e[1:]
            if from_ticks: assert (dx, dy) == offset(phase), ((dx, dy), offset(phase), phase)
            rows[p][r] = (dx, dy)
        elif e[0] == 'C':
            flag = e[1] ^ 1
            dst = np.zeros_like(src)
            for p in range(3):
                cur = None
                o = []
                # an offset seen at (p, r) holds until the next change; before the first change of this plane, the
                # last one of the previous plane
                for r in range(200):
                    if rows[p][r] is not None: cur = rows[p][r]
                    if cur is None:
                        cur = next((rows[q][rr] for q in range(p - 1, -1, -1) for rr in range(199, -1, -1)
                                    if rows[q][rr] is not None), None)
                    o.append(cur)
                dst[p] = zoom_plane(src[p], o)
            scr = convert(dst, flag)
            src = dst
        elif e[0] == 'E':
            out.append(scr)
    ref = ref[len(ref) - len(out):]
    bad = [i for i in range(len(out)) if not np.array_equal(out[i], ref[i])]
    print('%s (%s): %d screens, %d differ%s' % (path, 'ticks' if from_ticks else 'recorded', len(out), len(bad),
          (' first %d: %d pixels' % (bad[0], (out[bad[0]] != ref[bad[0]]).sum())) if bad else ''))
    return out


if __name__ == '__main__':
    run(sys.argv[1], '--ticks' in sys.argv)
