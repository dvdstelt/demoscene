"""Model of part 0x164dc's PIT callback 0x16646 (chrome star): object motion, morph key, palette levels.

t = [0x163ae] = PIT ticks since the callback was installed. Steps the original's state tick by tick (integer, as the
code) and compares with out/runs/star.log_at (logged at 0x165e0, once per drawn frame).
"""
import struct
import sys

import numpy as np

import snap

I = snap.IMG
T1 = np.frombuffer(I, np.int16, 64, 0x15960).astype(int)
T2 = np.frombuffer(I, np.int16, 64, 0x159e0).astype(int)
T3 = np.frombuffer(I, np.int16, 256, 0x15660).astype(int)


class Star:
    def __init__(self):
        self.t = 0
        self.x, self.y, self.z = 0, 0x16e360, struct.unpack('<i', bytes.fromhex('90c7c6ff'))[0]
        self.a, self.b, self.c = 0xfc00, 0, 0                      # angles (1/4096 turn), as 16-bit words
        self.mk, self.ms = 0, 0                                     # morph key 0..2, sub tick 0..127
        self.lv = [0, 0, 0]; self.sh = 6; self.upd = 0             # palette levels r, g, b, shift, update flag
        self.bspeed, self.interval, self.next, self.flash, self.dec = 9, 206, 560, 0, 4
        self.final, self.vz, self.n3 = 0, 0, 0

    def tick(self):
        self.upd = 0
        self.t += 1
        t = self.t
        self.a = (self.a + 10) & 0xfff
        self.b = (self.b + self.bspeed) & 0xfff
        self.c = (self.c + 6) & 0xfff
        self.ms += 1
        if self.ms == 128:
            self.ms = 0; self.mk = (self.mk + 1) % 3
        if t < 0x80:
            self.upd = 1; self.lv = [t, t, t]; self.sh = 7
        elif t <= 0x9e:
            pass
        elif t < 0x166:
            self.y = 0x173180 - (t - 0x9e) * 0x1d4c
        else:
            if self.flash:
                self.flash -= 1; self.upd = 1; self.sh = 6
                self.a = (self.a + 0x12) & 0xfff
                n = self.flash
                self.lv = [T2[n] + 0x2a & 255, T2[n] + 0x2a & 255, (T1[n] >> 1) + 0x30 & 255]
            elif self.final != 1 and t == self.next:
                self.flash = 0x3f
                self.interval -= self.dec; self.dec += 6; self.bspeed += 2
                if self.interval < 0x50:
                    self.interval = 0x50; self.final = 1; self.n3 = 0
                self.next += self.interval
        if self.final == 1:
            self.vz += 0xb9
            self.z += self.vz
            if self.vz > 0x23f0:
                self.sh = 6; self.n3 += 1
                v = T3[self.n3 & 0xff]
                self.lv = [v, v, v]; self.upd = 1
                if self.n3 >= 0xff:
                    self.n3 = 0xff; self.final = 2; self.lv = [0, 0, 0]


def s16(v): return (v + 0x8000 & 0xffff) - 0x8000


if __name__ == '__main__':
    rows = [list(map(int, l.split()[1:])) for l in open(sys.argv[1])]
    st = Star(); bad = 0; beats = []
    for r in rows:
        frame, pit, t, x, y, z, a, b, c, mk, ms, lr, lg, lb, sh, bsp, itv, nxt, fl, dec, fin, vz, n3, upd = r
        while st.t < t:
            st.tick()
            if st.flash == 0x3f: beats.append(st.t)
        got = [x, y, z, s16(a), s16(b), s16(c), mk, ms, bsp, itv, nxt, fl, dec, fin, vz, n3]
        exp = [st.x, st.y, st.z, s16(st.a), s16(st.b), s16(st.c), st.mk, st.ms, st.bspeed, st.interval, st.next,
               st.flash, st.dec, st.final, st.vz, st.n3]
        if got != exp:
            bad += 1
            if bad < 8: print('mismatch frame', frame, 't', t, got, exp)
    print(len(rows), 'frames compared,', bad, 'mismatches; beats (pulse starts) at ticks', beats,
          '; last t', rows[-1][2], '; PIT tick at t = 0:', rows[0][1] - rows[0][2])
