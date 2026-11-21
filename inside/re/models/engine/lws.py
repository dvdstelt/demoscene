"""The LightWave scene reader of the flower part (0x408030 .. 0x409358) as a Python model: the parser (0x408480,
0x408588, number readers 0x408d70 / 0x408dfc / 0x408f9c, keyword search 0x409050), the per-frame sampling of each
motion with the cardinal spline 0x4090cc, and the evaluation at a fractional frame (0x4080d8).

Checks the sampled table against the original's (0x426618 in a snapshot taken after the precalc):
  python3 lws.py ../../snaps/f38.snap
"""
import os
import pickle
import struct
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..'))

F = np.float32
DEG = 0.017452777777777779            # [0x42149d]: the code's degrees -> radians factor (pi/180 = 0.0174532925...)


class Cursor:
    def __init__(self, text):
        self.s = text
        self.p = 0

    def find(self, word):
        """0x409050: scan forward to the end of the next occurrence of word; -1 if there is none (the cursor then
        stays after the end of the text)"""
        i = self.s.find(word.encode(), self.p)
        if i < 0:
            self.p = len(self.s)
            return -1
        self.p = i + len(word)
        return self.p

    def _ch(self):
        c = self.s[self.p] if self.p < len(self.s) else 0
        self.p += 1
        return c

    def integer(self):
        """0x408d70: skip to a digit or '-', read decimal digits"""
        while True:
            c = self._ch()
            if c == 0x2d or 0x30 <= c <= 0x39:
                break
        sign = 1
        if c == 0x2d:
            sign = -1
            c = self._ch()
        v = 0
        while True:
            v = v * 10 + (c - 0x30)
            c = self._ch()
            if not 0x30 <= c <= 0x39:
                break
        return sign * v

    def real(self):
        """0x408dfc: [-]digits[.digits][e[-]ddd] as float32 arithmetic (value*10 + digit, fraction digits
        counted in a float32 divisor, an exponent of exactly three digits after 'e')"""
        while True:
            c = self._ch()
            if c == 0x2d or 0x30 <= c <= 0x39:
                break
        sign = F(1)
        if c == 0x2d:
            sign = F(-1)
            c = self._ch()
        ip = F(0)
        div = F(1)
        exp = 0.0
        while c not in (0x2e, 0x65):
            ip = F(float(ip) * 10.0 + (c - 0x30))
            c = self._ch()
            if c in (0x65, 0x2e) or 0x30 <= c <= 0x39:
                continue
            return F(float(sign) * float(ip))
        if c == 0x65:
            exp = self._exponent()
            return F(float(sign) * float(ip) * 10.0 ** exp)
        c = self._ch()
        while 0x30 <= c <= 0x39:
            ip = F(float(ip) * 10.0 + (c - 0x30))
            div = F(float(div) * 10.0)
            c = self._ch()
        if c == 0x65:
            exp = self._exponent()
        return F(float(sign) * float(ip) / float(div) * 10.0 ** exp)

    def _exponent(self):
        c = self._ch()
        sign = 1
        if c == 0x2d:
            sign = -1
            c = self._ch()
        d1 = c - 0x30
        d2 = self._ch() - 0x30
        d3 = self._ch() - 0x30
        self.p += 1
        return sign * (d1 * 100 + d2 * 10 + d3)


def spline(t, c, p0, p1, p2, p3):
    """0x4090cc: cardinal spline through p1 (t = 0) and p2 (t = 1), a = (1 - c) / 2 (c = [0x422868] = 0: Catmull-Rom)"""
    t = float(t)
    a = (1.0 - float(c)) * 0.5
    t2 = t * t
    t3 = t2 * t
    return (float(p1) * ((2.0 - a) * t3 + (a - 3.0) * t2 + 1.0)
            + a * float(p0) * (2.0 * t2 - t3 - t)
            + float(p2) * ((a - 2.0) * t3 + (3.0 - 2.0 * a) * t2 + a * t)
            + a * float(p3) * (t3 - t2))


def motion(cur, first, last, scale, c=0.0):
    """0x408588: read one motion (the channel count must be 9; n keys of x y z H P B sx sy sz frame linear tension
    continuity bias: the last four are ignored, so every key is a plain spline point) and sample it once per frame.
    Returns rows of 9 float32: x * scale, -y * scale, z * scale, P, H, B (radians, with the code's factor DEG),
    sx, sy, sz. Row 0 = row 1; rows N+1, N+2 repeat row N (the spline's neighbours at the ends)."""
    if cur.integer() != 9:
        return None
    n = cur.integer()
    slots = [None]
    for _ in range(n):
        v = [cur.real() for _ in range(9)]
        fr = cur.real()
        for _ in range(4):
            cur.real()
        slots.append(v[:6] + [fr] + v[6:])     # record: x y z H P B frame sx sy sz
    if int(np.trunc(slots[1][6])) != first:    # slot 1 := a copy of the first key at FirstFrame
        k = list(slots[1])
        k[6] = F(first)
        slots.insert(1, k)
        n += 1
    if int(np.trunc(slots[n][6])) != last:     # a copy of the last key at LastFrame
        k = list(slots[n])
        k[6] = F(last)
        slots.append(k)
        n += 1
    for _ in range(3):                         # three copies of the last key at its frame + 1
        k = list(slots[n])
        k[6] = F(float(slots[n][6]) + 1.0)
        slots.append(k)
    slots[0] = list(slots[1])
    rows = [None]
    for j in range(1, n + 1):                  # segments (slot j, slot j+1), neighbours j-1 and j+2
        steps = int(np.trunc(F(float(slots[j + 1][6]) - float(slots[j][6]))))
        if steps <= 0:
            continue
        dt = F(1.0 / steps)
        t = F(0)
        for _ in range(steps):
            def ch(i):
                return spline(t, c, slots[j - 1][i], slots[j][i], slots[j + 1][i], slots[j + 2][i])
            rows.append([F(ch(0) * float(F(scale))), F(ch(1) * -float(F(scale))), F(ch(2) * float(F(scale))),
                         F(ch(4) * DEG), F(ch(3) * DEG), F(ch(5) * DEG), F(ch(7)), F(ch(8)), F(ch(9))])
            t = F(float(t) + float(dt))
    rows[0] = list(rows[1])
    N = len(rows) - 1
    rows.append(list(rows[N]))
    rows.append(list(rows[N]))
    return rows, N


def read_scene(text, scale, c=0.0):
    """0x408480: 'LWSC', FirstFrame, LastFrame, then CameraMotion (motion 0) and every ObjectMotion after
    LastFrame in file order (motions 1, 2, ...). Returns (motions, N = the camera motion's sample count)."""
    cur = Cursor(text)
    if cur.find('LWSC') < 0:
        return None
    cur.find('FirstFrame')
    first = cur.integer()
    cur.find('LastFrame')
    last = cur.integer()
    mark = cur.p
    cur.find('CameraMotion')
    cam, N = motion(cur, first, last, scale, c)
    motions = [cam]
    cur.p = mark
    while cur.find('ObjectMotion') >= 0:
        m, _ = motion(cur, first, last, scale, c)
        motions.append(m)
    return motions, N


def evaluate(rows, N, frame, c=0.0):
    """0x4080d8 at frame f = [0x4b685c] (float): i = trunc(f), k = i % N, between rows k+1 and k+2.
    Returns position (ints, truncated) and the rotation angles; the object's matrix is
    I . Ry(H) . Rx(-P) . Rz(-B) (0x4108dc, 0x4145ac, 0x4145c4, 0x4145dc)."""
    i = int(np.trunc(F(frame)))
    k = int(np.fmod(i, N))
    t = F(float(F(frame)) - i)
    v = [F(spline(t, c, rows[k][m], rows[k + 1][m], rows[k + 2][m], rows[k + 3][m])) for m in range(9)]
    pos = [int(np.trunc(v[0])), int(np.trunc(v[1])), int(np.trunc(v[2]))]
    return pos, {'P': v[3], 'H': v[4], 'B': v[5], 'scale': v[6:9]}


def main():
    import datfile
    snap = pickle.load(open(sys.argv[1], 'rb'))
    mem = snap['mem']
    d = datfile.Dat(os.path.join(HERE, '..', '..', '..', 'original', 'INSIDE.DAT'))
    text = d.get('flower/tunnel1.lws')
    motions, N = read_scene(text, 3000.0)
    nm = struct.unpack_from('<i', mem, 0x4b6854)[0]
    Nmem = struct.unpack_from('<i', mem, 0x4b6858)[0]
    print('motions: model %d, original %d; samples: model %d, original %d' % (len(motions), nm, N, Nmem))
    bad = 0
    tot = 0
    for mi, rows in enumerate(motions):
        for r, row in enumerate(rows):
            orig = struct.unpack_from('<9f', mem, 0x426618 + 0x4800 * mi + 0x24 * r)
            tot += 1
            if any(np.float32(a) != b for a, b in zip(orig, row)):
                bad += 1
                if bad <= 5:
                    print('motion %d row %d: original %s model %s' % (mi, r, orig, [float(x) for x in row]))
    print('%d of %d rows differ' % (bad, tot))


if __name__ == '__main__':
    main()
