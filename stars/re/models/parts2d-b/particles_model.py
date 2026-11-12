"""Model of the particle parts 0x133ad (variant 1, blue) and 0x133f1 (variant 2, beige, with words and 4 orbiting
blobs): reproduces every drawn frame of a hooked run (run_hook.py + hook_particles.py) from the part's start.

usage: python3 particles_model.py runs/p1.pkl 1 SNAP [--ticks]     (variant 1 or 2; SNAP = the part's snapshot)
  default: the tick-driven inputs (tick counter n, text timer, word index) are taken at the instructions that read
  them; --ticks: they are recomputed from the PIT callbacks (with the music row) in the event log, starting from the
  snapshot's [0x13044] / [0x13539]: this checks the tick model (the remake's logic)."""
import sys, pickle
import numpy as np
import snap

IMG = snap.Mem(snap.IMG)
F58 = np.frombuffer(snap.ovl(58), np.uint8)


class Rng:                                   # 0x4af10, seed dword [0x4af35]
    def __init__(self, s): self.s = s
    def __call__(self, n):
        s = ((self.s ^ 0x78d47e5d) * 0xd413d473) & 0xffffffff
        self.s = ((s << 18) | (s >> 14)) & 0xffffffff
        return self.s % n


SIN = [IMG.s32(0xa740 + 4 * i) for i in range(4096 + 1024)]      # round(65528 sin(2 pi i / 4096)); cos = +1024

# column / row maps of the zoom (0x13922)
COLS = [((e * 0x78 + 0x40) >> 7) + 0xa0 for e in range(-0xa0, 0xa0, 4)]       # 80 groups of 4 pixels (0x12814)
ROWS = [((99 - j) * 0x78 >> 7) + 0x64 for j in range(200)]                    # 0x12d14, used backwards
SRC_IDX = (np.array([ROWS[199 - r] for r in range(200)])[:, None] * 320 +
           np.array([COLS[x // 4] + x % 4 for x in range(320)])[None, :]).ravel()


def zoom(src):
    """the generated code at [0x13040]: dst[r][4i+k] = (s[a-1] + s[a+1] + s[a-320] + s[a+320]) >> 2,
    a = ROWS[199-r]*320 + COLS[i] + k (values <= 31, so the packed dword sums never carry)"""
    s = src.astype(np.uint16)
    return ((s[SRC_IDX - 1] + s[SRC_IDX + 1] + s[SRC_IDX - 320] + s[SRC_IDX + 320]) >> 2).astype(np.uint8)


def blob(buf, off, val, moves):
    """the stos sequences of 0x138ff / 0x138be: edi -= 0x3be, then (count, advance) pairs"""
    e = off - 0x3be
    for cnt, adv in moves:
        buf[e:e + cnt] = val
        e += cnt + adv


SMALL = [(3, 0x13c), (5, 0x13b), (5, 0x13c), (3, 0)]                                         # 0x138ff
BIG = [(3, 0x13c), (5, 0x13a), (7, 0x139), (7, 0x139), (7, 0x13a), (5, 0x13b), (3, 0)]      # 0x138be
ORBITS = ((4, 10, 10, 0), (6, 12, 10, 0), (2, 11, 10, 1), (7, 11, 12, 0))   # (shl, sar y, sar x, negate n)


def orbit(n, k):
    sh, ys, xs, neg = ORBITS[k]
    a = ((-n if neg else n) << sh) & 0xfff
    return (SIN[a + 1024] >> xs) + 0xa0, (SIN[a] >> ys) + 0x64      # x, y


def text_entry(buf, variant, idx):
    base = 0x13178 if variant == 1 else 0x13278
    d, so, w, h = (IMG.u32(base + 16 * idx + 4 * k) for k in range(4))
    for r in range(h):
        s = F58[so + 320 * r: so + 320 * r + w]
        m = s != 0
        buf[d + 320 * r: d + 320 * r + w][m] = s[m]


def run(path, variant, snapname, from_ticks=False):
    h = pickle.load(open(path, 'rb')); ev = h['events']; ref = h['frames']
    sm = snap.Mem(snap.load(snapname)['mem'])
    src = np.zeros(64000, np.uint8)          # both buffers are cleared by the set-up (0x13922)
    rng = Rng(sm.u32(0x4af35))
    n, since, timer, idx = sm.u32(0x13044), sm.u32(0x13539), 0x14, 0     # set-up: [0x13170] = 20, [0x13174] = 0
    out = []; cur = {}
    for e in ev:
        if e[0] == 'T':                      # PIT callback: n++, since++ (0x1353d), fade step, row 0 / 0x1e -> 20
            n += 1; since += 1
            if e[3] in (0, 0x1e): timer = 0x14
        elif e[0] == 'F':
            assert rng.s == e[3], 'seed mismatch at frame %d' % e[1]
            for _ in range(10):              # ten random blobs (0x135c5)
                y = rng(100) + 0x32; x = rng(0xa0) + 0x50
                col = 0 if rng(0x100) & 1 else 0x1f
                blob(src, y * 320 + x, col, SMALL)
            rng(0xfa0)                       # 0x13610: a ring of 10 points is computed but never drawn
        elif e[0] == 't170':                 # 0x13676: the word
            if from_ticks:
                if timer != 0:
                    timer -= since
                    if timer < 0:
                        timer = 0; idx += 1
                        if idx >= (0x11 if variant == 2 else 0xd): idx = 0
                    since = 0
                    text_entry(src, variant, idx)
            elif e[1] != 0:
                cur['text'] = True
        elif e[0] == 'idx':
            if not from_ticks and cur.pop('text', False):
                text_entry(src, variant, e[1])
        elif e[0] == 'n':                    # variant 2: blob k at the tick counter as read
            x, y = orbit(n if from_ticks else e[1], e[2])
            blob(src, y * 320 + x, 0x1f, BIG)
        elif e[0] == 'E':
            dst = zoom(src)
            out.append(dst)
            src = dst.copy()                 # swap: the new frame is the next source
    out = np.array(out)
    bad = [i for i in range(len(out)) if not np.array_equal(out[i], ref[i])]
    print('%s variant %d (%s): %d frames, %d differ%s' % (path, variant, 'ticks' if from_ticks else 'recorded', len(out),
          len(bad), (' first %d: %d pixels' % (bad[0], (out[bad[0]] != ref[bad[0]]).sum())) if bad else ''))
    return out


if __name__ == '__main__':
    run(sys.argv[1], int(sys.argv[2]), sys.argv[3], '--ticks' in sys.argv)
