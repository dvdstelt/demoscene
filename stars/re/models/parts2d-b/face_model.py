"""Model of the face part 0x1089c (PIT callback 0x10ae2): reproduces every frame of a hooked run
(run_hook.py + hook_face.py).

usage: python3 face_model.py runs/f.pkl [recorded|formula|script]
  recorded: the warp tables as the program built them (checks remap, face drawing and warp);
  formula:  the tables from the phases / zoom / steps the frame read (checks the table formulas);
  script:   everything from the PIT callbacks and the music position in the event log, with the main loop's script
            (phases, zoom oscillator, light angle, parameter blocks, strobe, per-frame zoom steps): the remake's logic."""
import sys, pickle
import numpy as np
import snap
import face_data

IMG = snap.Mem(snap.IMG)
S = np.array([IMG.s32(0xffb8 + 4 * i) for i in range(512)], np.int64)      # round(1024 sin(2 pi i / 512))
SIN4K = [IMG.s32(0xa740 + 4 * i) for i in range(4096)]                    # round(65528 sin(2 pi i / 4096))
F35, PAL, TABLES = face_data.file35()
F35 = np.frombuffer(F35, np.uint8)


def span_list():
    """the converted list of 0x11a0a: [(skip, [words])]"""
    spans, _, _ = face_data.decode34()
    return [(sk, np.array(w, np.int64)) for sk, cnt, w, hdr in spans]


SPANS = span_list()
FACE_POS = []; FACE_WORD = []; REMAP_POS = []
_p = 0
for sk, w in SPANS:
    REMAP_POS.extend(range(_p, _p + sk)); _p += sk
    FACE_POS.extend(range(_p, _p + len(w))); FACE_WORD.extend(w); _p += len(w)
FACE_POS = np.array(FACE_POS); FACE_WORD = np.array(FACE_WORD); REMAP_POS = np.array(REMAP_POS)


def light_offset(angle):
    """0x10a86: only the cosine survives (eax is overwritten): rows of the env map"""
    e = (angle & 0x7fc) + 0x200 & 0x7fc
    return (int(S[e >> 2]) >> 5) * 256 + 0x2000


def remap_and_face(buf, tab, face, light):
    """0x1178c on the 64000-byte frame buf (in place)"""
    t = TABLES[tab]
    if face <= 0:
        buf[:] = t[buf]
    else:
        buf[REMAP_POS] = t[buf[REMAP_POS]]                  # the gaps between face spans (not the last 132 pixels)
        buf[FACE_POS] = F35[light + FACE_WORD]              # fs base = file 35 + light; offset = the 16-bit word


def warp_tables(ph, steps, z):
    """0x10bf3: ph = [0xf770..0xf77c], steps = [0xf748, 0xf750, 0xf758, 0xf760], z = [0xf764]"""
    cols = []; e1, e2 = ph[0], ph[1]; edx = ((0x100 - z) * 320) >> 1
    for x in range(320):
        e1 &= 0x7fc; e2 &= 0x7fc
        v = (int(S[e1 >> 2] + S[e2 >> 2]) * z) >> 16
        cols.append(v * 320 + (edx >> 8))
        edx += z; e1 += steps[0]; e2 += steps[1]
    rows = []; e3, e4 = ph[2], ph[3]; edx = ((0x100 - z) * 200) >> 1
    for y in range(200):
        e3 &= 0x7fc; e4 &= 0x7fc
        v = (int(S[e3 >> 2] + S[e4 >> 2]) * z) >> 16
        rows.append((edx >> 8) * 320 + v)
        edx += z; e3 += steps[2]; e4 += steps[3]
    return cols, rows


def u32(v): return v & 0xffffffff


def s32(v): v &= 0xffffffff; return v - (1 << 32) if v >> 31 else v


BLOCKS = {k: [IMG.s32(k + 4 * i) for i in range(16)] for k in (0x107dc, 0x1081c, 0x1085c)}


class Script:
    """the main loop of 0x1089c between frames, and the PIT callback 0x10ae2 (one call per tick)"""
    def __init__(self, sm):
        self.p = [sm.s32(0xf740 + 4 * i) for i in range(16)]       # the parameter block 0xf740..0xf77f
        self.angle, self.aspeed = sm.u32(0x107ca), sm.u32(0x107ce)
        self.osc = dict(ph=sm.u32(0x11a8d), spd=sm.u32(0x11a91), mid=sm.s32(0x11a95), amp=sm.s32(0x11a99),
                        on=sm.u32(0x11aa1))
        self.phase = 0; self.acc, self.inc = sm.u16(0x10a7e), sm.u16(0x10a80); self.cnt = sm.u32(0x10a82)
        self.extra = False; self.fade_out = False; self.done = False

    def tick(self):                                      # 0x10ae2
        self.angle = u32(self.angle + self.aspeed)
        for i in range(4): self.p[12 + i] = s32(self.p[12 + i] + self.p[1 + 2 * i])
        if self.osc['on']:                               # 0x11ad8: z = mid + amp * sin >> 16
            self.osc['ph'] = u32(self.osc['ph'] + self.osc['spd'])
            self.p[9] = ((SIN4K[self.osc['ph'] & 0xfff] * self.osc['amp']) >> 16) + self.osc['mid']

    def load(self, k): self.p = list(BLOCKS[k])

    def before_frame(self):                              # actions at the top of a loop iteration
        if self.phase == 8 and self.cnt:
            self.cnt -= 1
            if self.cnt & 3 == 0: self.p[9] += 1

    def after_frame(self, pos):                          # the checks after a frame (pos = order << 8 | row)
        ph = self.phase
        if ph == 0 and pos >= 0x8820:   self.osc['on'] = 0; self.aspeed = 0x50; self.phase = 1
        elif ph == 1 and self.angle >= 0x980: self.aspeed = 0; self.load(0x107dc); self.phase = 2
        elif ph == 2 and pos > 0x8920:  self.phase = 3
        elif ph == 3 and pos > 0x8b00:  self.aspeed = 0x1e; self.phase = 4
        elif ph == 4 and pos > 0x8b20:  self.load(0x1081c); self.phase = 5
        elif ph == 5 and pos > 0x8d00:  self.p[11] = 0; self.phase = 6
        elif ph == 6:
            if not self.extra:
                self.acc += self.inc
                if self.acc > 0xffff:                    # carry: one more frame with the face
                    self.acc &= 0xffff; self.p[11] = 1; self.extra = True; return
            else:
                self.p[11] = 0; self.extra = False
            self.inc = (self.inc + 0x32) & 0xffff
            if pos > 0x8f00: self.p[11] = 1; self.phase = 7
        elif ph == 7 and pos > 0x9000:  self.load(0x1085c); self.cnt = 0x30; self.phase = 8
        elif ph == 8 and pos > 0x9200:  self.p[11] = 0; self.p[9] += 0x16; self.phase = 9
        elif ph == 9 and pos > 0x9210:  self.fade_out = True; self.phase = 10
        elif ph == 10 and pos > 0x9230: self.done = True


def run(path, mode):
    h = pickle.load(open(path, 'rb')); ev = h['events']; ref = h['frames']
    sm = snap.Mem(snap.load('f23436_1089c.snap')['mem'])
    base = sm.u32(0xffa0)
    blk = np.frombuffer(sm.blk(base, 0x2ee00), np.uint8).copy()
    cur, oth = sm.u32(0xffa4) - base, sm.u32(0xffa8) - base
    sc = Script(sm)
    out_bad = []; tab_bad = 0; st_bad = []; n = 0; light = None; started = False
    last_block = blockv = list(sc.p)
    for e in ev:
        if e[0] == 'T':
            sc.tick()
        elif e[0] == 'F':
            started = True
            if mode == 'script':
                sc.before_frame()
                rec = e[5]
                mine = sc.p
                if mine != rec or sc.angle != e[6]:
                    st_bad.append((n, e[1], mine, rec, sc.angle, e[6]))
                angle = sc.angle; blockv = mine
            else:
                angle = e[6]; blockv = e[5]
            light = light_offset(angle)
        elif e[0] == 'R':
            if light is None: light = e[3]                  # the first frame started before the snapshot
            tab, face = (sc.p[10], sc.p[11]) if (mode == 'script' and started) else (e[1], e[2])
            if mode != 'script': assert light == e[3], (light, e[3])
            remap_and_face(blk[cur:cur + 64000], tab, face, light)
        elif e[0] == 'W0':
            W0 = e
            p_w0 = list(sc.p)
        elif e[0] == 'W1':
            W1 = e
            p_w1 = list(sc.p)
        elif e[0] == 'W':
            if mode == 'recorded':
                cols, rows = e[1], e[2]
            else:
                p = sc.p if (mode == 'script' and started) else None    # read after the retrace wait
                ph = (W0[1][:2] + W1[1]) if p is None else p_w0[12:14] + p_w1[14:16]   # columns: phases at entry; rows: at 0x10c6a
                z = W0[2] if p is None else p_w0[9]
                steps = ([sc.p[2], sc.p[4], sc.p[6], sc.p[8]] if p is not None else
                         [last_block[2], last_block[4], last_block[6], last_block[8]])
                cols, rows = warp_tables(ph, steps, z)
                if (cols, rows) != (e[1], e[2]):
                    tab_bad += 1
                    if tab_bad <= 3:
                        print('warp table mismatch at frame', n)
                if mode == 'formula': cols, rows = e[1], e[2]   # render with the program's tables; count the misses
            idx = cur + np.add.outer(np.array(rows), np.array(cols)).ravel()
            blk[oth:oth + 64000] = blk[idx]
        elif e[0] == 'E':
            if not np.array_equal(blk[oth:oth + 64000], ref[n]): out_bad.append(n)
            cur, oth = oth, cur
            n += 1
            if mode == 'script': sc.after_frame(e[2] << 8 | e[3])
        if e[0] == 'F': last_block = e[5]
    print('%s (%s): %d frames, %d differ%s; warp tables differing from the formula: %d; script state mismatches: %d%s'
          % (path, mode, n, len(out_bad), (' (first %d)' % out_bad[0]) if out_bad else '', tab_bad, len(st_bad),
             (' first: %s' % (st_bad[0],)) if st_bad else ''))
    return st_bad


if __name__ == '__main__':
    run(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else 'recorded')
