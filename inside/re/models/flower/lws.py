"""Model of the flower part's LightWave scene reader (0x408030 / 0x408480 / 0x408588 / 0x4090cc) and of the per-frame
camera evaluation (0x4080d8). Checks the precomputed key table against the snapshot (0x426618).

usage (inside the toolbox, from inside/re): python3 models/flower/lws.py [snaps/f4963.snap]
"""
import os
import pickle
import struct
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
RE = os.path.join(HERE, '..', '..')
sys.path.insert(0, RE)
import datfile  # noqa: E402

f32 = np.float32
DEG = 0.0174527778  # double at 0x42149d (3.1415 / 180)


def spline(t, a, p0, p1, p2, p3):
    """0x4090cc: cardinal spline with c = (1 - a) / 2 (a = [0x422868] = 0: Catmull-Rom), x87 extended precision,
    result rounded to float by the caller's fstp."""
    t = float(t)
    c = (1.0 - float(a)) * 0.5
    t2 = t * t
    t3 = t2 * t
    return (p0 * c * (2 * t2 - t3 - t) + p1 * ((2 - c) * t3 + (c - 3) * t2 + 1)
            + p2 * ((c - 2) * t3 + (3 - 2 * c) * t2 + c * t) + p3 * c * (t3 - t2))


class Reader:
    """the text cursor [0x4b6850] with the readers 0x409050 (find), 0x408d70 (int) and 0x408dfc (float)."""
    def __init__(self, text):
        self.t = text + '\0'
        self.p = 0

    def find(self, word):
        """0x409050: scan for the first character, compare the rest; on a match the cursor stays just after the
        first character. Returns -1 when the text ends."""
        while True:
            ch = self.t[self.p]; self.p += 1
            if ch == '\0':
                return -1
            if ch == word[0] and self.t[self.p:self.p + len(word) - 1] == word[1:]:
                return self.p

    def _skip(self):
        while True:
            ch = self.t[self.p]; self.p += 1
            if ch == '-' or ch.isdigit():
                return ch

    def int(self):
        ch = self._skip()
        sign = 1
        if ch == '-':
            sign = -1; ch = self.t[self.p]; self.p += 1
        v = 0
        while True:
            v = v * 10 + ord(ch) - 48
            ch = self.t[self.p]; self.p += 1
            if not ch.isdigit():
                return sign * v

    def float(self):
        """mantissa and divisor accumulate digit by digit in float32; result float32(sign * mant / div)."""
        ch = self._skip()
        sign = 1.0
        if ch == '-':
            sign = -1.0; ch = self.t[self.p]; self.p += 1
        mant, div = f32(0), f32(1)
        while ch not in '.e':
            mant = f32(float(mant) * 10.0 + (ord(ch) - 48))
            ch = self.t[self.p]; self.p += 1
            if ch not in '.e' and not ch.isdigit():
                return f32(sign * float(mant))
        assert ch == '.'
        while True:
            ch = self.t[self.p]; self.p += 1
            if not ch.isdigit():
                break
            mant = f32(float(mant) * 10.0 + (ord(ch) - 48))
            div = f32(float(div) * 10.0)
        assert ch != 'e'
        return f32(sign * float(mant) / float(div))


def read_motions(text):
    """0x408480: FirstFrame, LastFrame, the CameraMotion keys, then (from the start of the file again) every
    ObjectMotion's keys. A key is 9 values (x y z h p b sx sy sz) and 5 more (frame linear tension continuity
    bias); the reader keeps the frame and ignores linear / TCB."""
    r = Reader(text)
    assert r.find('LWSC') >= 0
    r.find('FirstFrame'); first = r.int()
    r.find('LastFrame'); last = r.int()

    def keys():
        assert r.int() == 9
        n = r.int()
        ks = []
        for i in range(n):
            v = [r.float() for _ in range(10)]      # x y z h p b sx sy sz frame
            for _ in range(4):
                r.float()
            ks.append(v[:6] + [v[9]] + v[6:9])      # record order: x y z h p b frame sx sy sz
        return ks
    r.find('CameraMotion')
    motions = [keys()]
    r.p = 0
    while r.find('ObjectMotion') >= 0:
        motions.append(keys())
    return first, last, motions


def build_table(keys, first, last, scale, a=0.0):
    """0x408588: one record of 9 floats per LightWave frame: x*s, -y*s, z*s, p, h, b (radians, pi = 3.1415), sx, sy,
    sz. Returns (table [n+3][9] float32, n)."""
    # records: x y z h p b frame sx sy sz (float32)
    rec = [None] + [list(k) for k in keys]
    cnt = len(keys)
    if int(rec[1][6]) != first:              # trunc
        rec.insert(2, list(rec[1]))           # shift up, record 1 keeps its values with frame = FirstFrame
        rec[1][6] = f32(first)
        cnt += 1
    if int(rec[cnt][6]) != last:
        r = list(rec[cnt]); r[6] = f32(last)
        rec.append(r); cnt += 1
    rec[0] = list(rec[1])
    for j in range(3):
        r = list(rec[cnt]); r[6] = f32(1.0 + float(rec[cnt][6]))
        rec.append(r)
    out = np.zeros((600, 9), np.float32)
    k = 0
    order = [(0, scale), (1, -scale), (2, scale), (4, DEG), (3, DEG), (5, DEG), (7, 1), (8, 1), (9, 1)]
    for i in range(1, cnt + 1):
        span = f32(rec[i + 1][6] - rec[i][6])
        steps = int(span)                      # trunc
        inv = f32(1.0 / float(span))
        t = f32(0.0)
        for j in range(steps):
            k += 1
            for o, (c, m) in enumerate(order):
                v = spline(t, a, float(rec[i - 1][c]), float(rec[i][c]), float(rec[i + 1][c]), float(rec[i + 2][c]))
                out[k, o] = f32(v * m) if m != 1 else f32(v)
            t = f32(t + inv)
    out[0] = out[1]
    out[k + 1] = out[k]
    out[k + 2] = out[k]
    return out[:k + 3], k


def camera_at(table, n, f, a=0.0):
    """0x4080d8 for one motion: f = [0x4b685c] (float LightWave frame). Returns (x, y, z) ints and the angles
    (pitch, heading, bank) as float32 as the code keeps them."""
    fi = int(f)
    idx = fi % n + 1
    fr = f32(f32(f) - f32(fi))
    vals = []
    for c in range(9):
        v = spline(fr, a, float(table[idx - 1, c]), float(table[idx, c]), float(table[idx + 1, c]),
                   float(table[idx + 2, c]))
        vals.append(v)
    pos = tuple(int(np.trunc(v)) for v in vals[:3])
    ang = tuple(f32(v) for v in vals[3:6])
    return pos, ang


def tables(verbose=False):
    """0x408030(\"flower/tunnel1.lws\", 3000.0): the file's last byte (offset 0xa46) is overwritten by 0."""
    d = datfile.Dat(os.path.join(RE, '..', 'original', 'INSIDE.DAT'))
    text = d.get('flower/tunnel1.lws')[:0xa46].decode('latin1')
    first, last, motions = read_motions(text)
    tabs = [build_table(m, first, last, 3000.0) for m in motions]
    if verbose:
        print('FirstFrame', first, 'LastFrame', last, 'motions', len(motions), [len(m) for m in motions])
        print('frames per table', [t[1] for t in tabs])
    return tabs


def main():
    tabs = tables(True)
    snap = sys.argv[1] if len(sys.argv) > 1 else os.path.join(RE, 'snaps', 'f4963.snap')
    if os.path.exists(snap):
        import unicorn  # noqa: F401  (needed to unpickle)
        s = pickle.load(open(snap, 'rb'))
        mem = s['mem']
        for m, (tab, n) in enumerate(tabs):
            base = 0x426618 + m * 0x4800
            got = np.frombuffer(mem[base: base + 36 * (n + 3)], np.float32).reshape(n + 3, 9)
            diff = np.abs(got.astype(np.float64) - tab.astype(np.float64))
            bits = np.count_nonzero(got.view(np.uint32) != tab.view(np.uint32))
            print('motion %d: %d records, max |diff| %.3g, %d of %d floats differ in bits' % (
                m, n + 3, diff.max(), bits, got.size))
            if bits:
                w = np.argwhere(got.view(np.uint32) != tab.view(np.uint32))
                for r, c in w[:10]:
                    print('   rec %d col %d: got %r model %r' % (r, c, got[r, c], tab[r, c]))
        nframes = struct.unpack('<i', mem[0x4b6858:0x4b685c])[0]
        nmot = struct.unpack('<i', mem[0x4b6854:0x4b6858])[0]
        print('[0x4b6858] frames =', nframes, '[0x4b6854] motions =', nmot)
    return tabs


if __name__ == '__main__':
    main()
