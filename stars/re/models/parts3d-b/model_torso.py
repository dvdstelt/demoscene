"""Model of part 0x16f7c (torso, ring and mirror): the frame routine 0x17084 (phases by tick count) and the PIT
callback 0x17513, as integer code. The frame routine runs once per drawn frame, so the model takes the frame times
(the tick count [0x16d56] seen by each frame) from out/runs/torso2.log_at (logged at 0x16fbd) and compares every logged variable.
"""
import sys

import numpy as np

import snap

COS = np.frombuffer(snap.IMG, np.int16, 4096, 0x5599c).astype(int)     # trunc(1023 cos(2 pi i / 4096))


def tdiv(a, b): return int(a / b) if (a >= 0) == (b > 0) else -(abs(a) // abs(b))


def w16(v): return (v + 0x8000 & 0xffff) - 0x8000


class Torso:
    def __init__(self):
        self.t = 0
        # object 0x16cc8 (torso + ring, file 46): x y z (1/1024 units), angles a b c
        self.tx, self.ty, self.tz = 0, 0x113000, -0x23f000
        self.ta, self.tb, self.tc = 0, 0, 0
        # object 0x16ca8 (mirror, file 45)
        self.mx, self.my, self.mz = -0x1f4000, 0, -0x5dc000
        self.ma, self.mb, self.mc = 0, 0, 0
        self.ringy, self.ringx, self.ringz = 200, 0, 0              # [0x16d7e], [0x1733e], [0x17340]
        self.mirror, self.spin, self.palflag, self.fade = 0, 0, 1, 0  # [0x1707f], [0x17080], [0x1707e], [0x1707b]
        self.palcopied, self.end = 0, 0

    def tick(self):                                              # 0x17513
        self.t += 1
        if self.spin: self.tb = w16(self.tb - 12)
        if self.mirror == 1:
            self.mb = w16(self.mb - 7); self.ma = w16(self.ma + 1); self.mc = w16(self.mc + 3)
            self.tx = min(self.tx + 0x307, 0x64000)
            self.mx = min(self.mx + 0xda2, -0x4b000)
            self.mz = min(self.mz + 0x1e4d, -0x271000)
        if self.mirror == 2:
            self.mb = w16(self.mb - 8); self.ma = w16(self.ma + 1); self.mc = w16(self.mc + 4)
            self.tx -= 0x5c1; self.mx += 0x2ba

    def wobble(self):                                            # 0x17253
        k = self.t - 0x1f0
        self.ta = COS[((k << 6) - 0x400) & 0xfff] >> 2
        self.ringx = COS[((k << 7) - 0x400) & 0xfff] >> 3
        self.ringz = COS[(k << 7) & 0xfff] >> 3
        self.ringy = COS[(((self.t - 0x130) << 4) - 0x400) & 0xfff] >> 5

    def frame(self):                                             # 0x17084 (+ 0x173a1 sets the torso z)
        t = self.t
        if t < 0x40:
            self.palflag = 1; self.fade = t
        elif t < 0xa0:
            if not self.palcopied:
                self.palflag = 1; self.palcopied = 1
            else:
                self.palflag = 0
                self.tb = w16(tdiv(-0x800 * (t - 0x40), 0x60) + 0x800)
                self.ty = tdiv(-0x113000 * (t - 0x40), 0x60) + 0x113000
        elif t < 0x130:
            self.spin = 1
            self.ringy = tdiv(-200 * (t - 0xa0), 0x90) + 200
        elif t < 0x1f0:
            self.ringx = self.ringz = 0
            self.ringy = COS[(((t - 0x130) << 4) - 0x400) & 0xfff] >> 5
        elif t < 0x2e0:
            self.wobble()
        elif t < 0x640:
            self.wobble(); self.mirror = 1
        elif t < 0x910:
            self.mirror = 2; self.wobble()
        else:
            b = 0x98f - t
            self.palflag = 1
            if b < 0x7f:                             # at t = 0x910 exactly nothing else is updated
                if b <= 0:
                    self.end = 1; b = 0
                self.fade = b
                self.mirror = 2; self.wobble()
        if self.mirror:
            self.tz = -0x258000


if __name__ == '__main__':
    rows = [list(map(int, l.split()[1:])) for l in open(sys.argv[1])]
    m = Torso(); bad = 0
    for r in rows:
        frame, pit, t = r[:3]
        while m.t < t: m.tick()
        m.frame()                                    # logged at 0x16fbd, just after 0x17084 ran
        got = r[3:]
        # (the torso z is set to -0x258000 by 0x173a1, later in the same frame: the first mirror frame logs the old z)
        exp = [m.tx, m.ty, m.tz, m.ta, m.tb, m.tc, m.mx, m.my, m.mz, m.ma, m.mb, m.mc, m.ringy, m.ringx, m.ringz,
               m.mirror, m.spin, m.palflag, m.fade]
        if got != exp and not (got[:2] + got[3:] == exp[:2] + exp[3:] and got[2] == -0x23f000):
            bad += 1
            if bad < 8: print('mismatch frame', frame, 't', t, '\n got', got, '\n exp', exp)

    print(len(rows), 'frames compared,', bad, 'mismatches; PIT tick at t = 0:', rows[0][1] - rows[0][2],
          '; last t', rows[-1][2])
