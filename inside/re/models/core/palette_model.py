"""Model of the palette system (0x400364 .. 0x400612) checked against every DAC record of full.frames.

State: base[768] (6-bit), current[768] (what the DAC shows), float palette fl[768] (float32), adders[768]
(float32), steps left n, fade level L (float32).
  fade(L, n)    0x4004ac: n <= 0 -> 1; r = 1/n; fl = current; add = f32((base*L - fl) * r), every x87 result
                rounded to 53 bits (Watcom's start-up sets the control word to 0x127f: nearest, double precision;
                --pc64 uses 64 bits instead, which gives the same DACs here)
  set(pal)      0x400364: base = min(pal, 63); fade(L_last, n_left)
  flash         0x400498: current = min(base + 63, 63); fade(1.0, 15)
  inverse       0x400454: current = (63 - base) & 63; fade(1.0, 20)
  black         0x400438: fade(0.0, 40)
  step          0x400540, in the retrace callback: if n > 0: n -= 1; fl = f32(fl + add); clamp 0..63;
                current = trunc(fl); DAC = current
Script events are applied before the step of the first retrace callback after them: at record k (the record that
first shows the entry done) or k + 1; the model takes the one that matches (the arithmetic is what is checked).
Record 11967 had its retrace callback's frame part skipped ([0x4248c0] held by the temple's frame routine).
"""
import os
import pickle
import struct
import sys
from fractions import Fraction

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
RE = os.path.join(HERE, '..', '..')
sys.path.insert(0, RE)
import unicorn  # noqa: F401
import datfile
import rep
from sheet import read_ticks, open_frames, indexed

T = read_ticks(os.path.join(RE, 'full.ticks'))
raw, nrec = open_frames(os.path.join(RE, 'full.frames'))
D = datfile.Dat(os.path.join(RE, '..', 'original', 'INSIDE.DAT'))
SKIPPED = {11967}
PC = 64 if '--pc64' in sys.argv else 53          # x87 precision control (mantissa bits)


def round_bits(x, bits):
    """Round a Fraction to `bits` significant bits, nearest-even (x87 / IEEE)."""
    if x == 0:
        return Fraction(0)
    s = -1 if x < 0 else 1
    x = abs(x)
    e = x.numerator.bit_length() - x.denominator.bit_length()
    if Fraction(2) ** e > x:
        e -= 1
    q = x / Fraction(2) ** (e - bits + 1)          # in [2^(bits-1), 2^bits)
    n = q.numerator // q.denominator
    rem = q - n
    if rem > Fraction(1, 2) or (rem == Fraction(1, 2) and n % 2):
        n += 1
    return s * n * Fraction(2) ** (e - bits + 1)


def f32(x):
    return float(np.float32(float(round_bits(Fraction(x), 24))))


class Pal:
    def __init__(self, snap):
        m = snap['mem']
        p = lambda a: struct.unpack_from('<I', m, a)[0]
        self.base = np.frombuffer(m, np.uint8, 768, p(0x424890)).astype(np.int32).copy()
        self.cur = np.frombuffer(m, np.uint8, 768, p(0x4248a4)).astype(np.int32).copy()
        self.fl = np.frombuffer(m, np.float32, 768, p(0x4248b8)).copy()
        self.add = np.frombuffer(m, np.float32, 768, p(0x424884)).copy()
        self.n = struct.unpack_from('<i', m, 0x4248ac)[0]
        self.level = struct.unpack_from('<f', m, 0x422790)[0]
        self.mem = m

    def fade(self, level, n):
        if n <= 0:
            n = 1
        r = round_bits(Fraction(1, n), PC)
        lv = Fraction(level)
        self.fl = self.cur.astype(np.float32)
        cache = {}
        add = np.empty(768, np.float32)
        for i in range(768):
            key = (int(self.base[i]), int(self.cur[i]))
            if key not in cache:
                d = round_bits(key[0] * lv, PC) - key[1]          # fild base; fmul level; fsub fl
                cache[key] = f32(round_bits(round_bits(d, PC) * r, PC))
            add[i] = cache[key]
        self.add = add
        self.level = level
        self.n = n

    def set(self, pal):
        self.base = np.minimum(np.frombuffer(pal, np.uint8, 768).astype(np.int32), 63)
        self.fade(self.level, self.n)

    def flash(self):
        self.cur = np.clip(self.base + 63, 0, 63)
        self.fade(1.0, 15)

    def inverse(self):
        self.cur = (63 - self.base) & 63
        self.fade(1.0, 20)

    def black(self):
        self.fade(0.0, 40)

    def step(self):
        if self.n <= 0:
            return
        self.n -= 1
        fl = (self.fl.astype(np.float64) + self.add.astype(np.float64)).astype(np.float32)
        fl = np.where(fl < 0, np.float32(0), fl)
        fl = np.where(fl > 63, np.float32(63), fl)
        self.fl = fl.astype(np.float32)
        self.cur = np.trunc(self.fl).astype(np.int32)


SNAP38 = pickle.load(open(os.path.join(RE, 'snaps', 'f38.snap'), 'rb'))
BLOCK = struct.unpack_from('<I', SNAP38['mem'], 0x424894)[0]
PALS = {}          # the palettes as the program holds them in its data block after init (inits modify some)


def pal_file(name, zero0=False):
    """The 768 bytes of a .pal as they are in memory; zero0 = 0x40a098 sets entry 0 to black in place."""
    if name not in PALS:
        o = D.files[name][0]
        PALS[name] = bytearray(SNAP38['mem'][BLOCK + o:BLOCK + o + 768])
    if zero0:
        PALS[name][0:3] = b'\0\0\0'
    return bytes(PALS[name])


# palette actions of the script routines, in table order (entry index -> list of actions)
flower_count = [0]


def flower_event(p):          # 0x409358: [0x42294c] = ([0x42294c] + 1) % 9; even -> flash, odd -> inverse
    flower_count[0] = (flower_count[0] + 1) % 9
    (p.flash if flower_count[0] % 2 == 0 else p.inverse)()


twirl_case = [0]
TWIRL = {1: 'nigga/nigga2.pal', 2: 'nigga/nigga4.pal', 3: 'flower/jeesus.pal', 4: 'flight/map.pal',
         0: 'flower/jeesus.pal'}


def twirl_event(p):           # 0x40a098: next picture; palette entry 0 forced black; flash, then the new base
    twirl_case[0] = (twirl_case[0] + 1) % 5
    p.flash()
    p.set(pal_file(TWIRL[twirl_case[0]], zero0=True))


ACT = {
    0x400498: lambda p: p.flash(),
    0x400454: lambda p: p.inverse(),
    0x400438: lambda p: p.black(),
    0x403164: lambda p: p.set(pal_file('logo/logo.pal')),
    0x4073c8: lambda p: p.set(pal_file('credits/tunnel.pal')),
    0x405f1c: lambda p: p.flash(),
    0x4057bc: lambda p: p.set(pal_file('nigga/nigga2.pal')),
    0x405ba0: lambda p: p.set(pal_file('nigga/nigga4.pal')),
    0x4098c4: lambda p: p.set(pal_file('flower/jeesus.pal')),
    0x409358: flower_event,
    0x4093f0: lambda p: p.flash(),
    0x402c28: lambda p: (p.set(pal_file('flight/map.pal')), p.fade(1.0, 60)),
    0x407f90: lambda p: p.set(pal_file('water/water.pal')),
    0x40585c: lambda p: p.set(pal_file('nigga/nigga9.pal')),
    0x40571c: lambda p: p.set(pal_file('nigga/nigga1.pal')),
    0x40a678: lambda p: (p.flash(), p.set(pal_file('flower/jeesus.pal'))),
    0x40a098: twirl_event,
    0x40461c: lambda p: (p.flash(), p.set(pal_file('temple/temple.pal'))),
}

entries = []
a = 0x4222e4
while True:
    o, r, f = rep.rd32(a), rep.rd32(a + 4), rep.rd32(a + 8)
    entries.append((o, r, f))
    a += 12
    if f == 0:
        break
ks = sorted(T)
done_at = {}
for i in range(len(entries) - 1):
    done_at[i] = next((k for k in ks if T[k].script > i), None)
# passes: entries run in the same main-loop pass share a record; entry 88 (30:12) runs with 87 (30:48)
passes = []
for i in range(len(entries) - 1):
    o, r, f = entries[i]
    if f not in ACT or done_at[i] is None:
        continue
    same = passes and (passes[-1]['pos'] == (o, r) or (i == 88 and passes[-1]['pos'] == (30, 48)))
    if same:
        passes[-1]['ev'].append(i)
    else:
        passes.append({'pos': (o, r), 'ev': [i], 'k': done_at[i]})
    # the record of the pass: the first record that shows the first of its entries done
    passes[-1]['k'] = min(passes[-1]['k'], done_at[i])

p = Pal(SNAP38)
assert np.array_equal(p.cur, indexed(raw, 38)[1].reshape(-1)), 'snapshot state is record 38'
pi = 0
bad = 0
checked = 0
choice = []
k = 39
while k < nrec:
    cand = None
    if pi < len(passes) and passes[pi]['k'] in (k, k - 1):
        cand = passes[pi]
    if cand is not None:
        import copy
        saved = (copy.deepcopy(p.__dict__), flower_count[0], twirl_case[0])
        for i in cand['ev']:
            ACT[entries[i][2]](p)
        if k not in SKIPPED:
            p.step()
        if np.array_equal(p.cur, indexed(raw, k)[1].reshape(-1)) or cand['k'] == k - 1:
            choice.append((cand['ev'], k))
            pi += 1
        else:
            # not yet: undo and apply at k + 1
            p.__dict__, flower_count[0], twirl_case[0] = saved
            p.__dict__ = saved[0]
            if k not in SKIPPED:
                p.step()
    elif k not in SKIPPED:
        p.step()
    dacv = indexed(raw, k)[1].reshape(-1)
    checked += 1
    if not np.array_equal(p.cur, dacv):
        bad += 1
        if bad <= 10:
            diff = np.nonzero(p.cur != dacv)[0]
            print('record %d: %d entries differ, e.g. %s model %s capture %s' % (
                k, len(diff), diff[:6], p.cur[diff[:6]], dacv[diff[:6]]))
    k += 1
print('records checked %d (38..%d), mismatching records %d' % (checked, nrec - 1, bad))
import json
json.dump({str(i): kk for ev, kk in choice for i in ev}, open(os.path.join(HERE, 'palette_steps.json'), 'w'))
for ev, kk in choice:
    print('entries %s: first fade step at record %d (retrace callback M = %d)' % (
        ev, kk, 4 * kk - 150 - (1 if kk >= 3045 else 0)))
if '--passes' in sys.argv:
    for ps in passes:
        print(ps)
