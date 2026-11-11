"""colour.py: models of the colour tables and the 16-bit pixel converters, checked against snapshots.
   0x1a336 (768-entry R/G/B tables at 0x1a479 / 0x1b079 / 0x1bc79: brightness E, contrast S = [0x1a225],
   inversion F = [0x1a478]), 0x1a236 (512-entry tables at 0x1c879 / 0x1d079 / 0x1d879), 0x11f73 (TBL's 32-entry
   LUT at 0x11eef), 0x13d73 (planar RGB -> 16-bit texture), 0x13eec (planar RGB -> interleaved RGB)."""
import sys, struct
import numpy as np
sys.path.insert(0, '/home/dennis/src/dvdstelt/demoscene/jizz/re/models/core')
from snap import load, rd32, rds32, rdb

SIZE = (5, 6, 5); POS = (11, 5, 0)          # 5:6:5 as main stores it at 0x18d70.. / 0x18d7c..

def flash(v, F):
    """the lerp of 0x1a236 / 0x1a336: v (0..255) towards 255 - v by F/256 (8-bit muls, then >> 8)"""
    return ((255 - F) * v + v + (255 - v) * F) >> 8

def tables768(E, S, F):
    out = []
    for c in range(3):
        t = []
        for i in range(0x300):
            v = (i * S + (E << 8)) >> 8          # sar (E signed)
            v = 0 if v < 0 else min(v, 255)
            v = flash(v, F)
            t.append(((v << SIZE[c]) >> 8) << POS[c])
        out.append(t)
    return out

def tables512(F):
    out = []
    for c in range(3):
        out.append([((flash(min(i, 255), F) << SIZE[c]) >> 8) << POS[c] for i in range(0x200)])
    return out

def lut_11f73(base, lvl):
    out = []
    for i in range(32):
        bl = min(max(i + lvl, 0), 31)
        px = 0
        for c in range(3):
            al = min(base[c] + 8 * bl, 255)
            px |= ((al * bl) >> (13 - SIZE[c])) << POS[c]
        out.append(px)
    return out

def to16(planar):
    """0x13d73: 256x256 planar R, G, B bytes -> 65536 words (r >> 3) << 11 | (g >> 2) << 5 | (b >> 3)"""
    r, g, b = (planar[k * 0x10000:(k + 1) * 0x10000].astype(np.uint32) for k in range(3))
    return ((r >> 3) << 11 | (g >> 2) << 5 | (b >> 3)).astype(np.uint16)

def to_rgb(planar):
    """0x13eec: planar -> interleaved r, g, b bytes"""
    return np.stack([planar[k * 0x10000:(k + 1) * 0x10000] for k in range(3)], -1).reshape(-1)

if __name__ == '__main__' and len(sys.argv) == 1:
    for tick, E, S in ((1365, -200, 0x1c8), (2515, 0, 0x100), (3285, 0, 0x100), (4435, -40, 0x100), (7506, -50, 0x132)):
        s = load(tick)
        F = s['mem'][0x1a478]
        want = [struct.unpack_from('<768I', s['mem'], a) for a in (0x1a479, 0x1b079, 0x1bc79)]
        got = tables768(E, S, F)
        d = sum(np.count_nonzero(np.array(g) != np.array(w)) for g, w in zip(got, want))
        want5 = [struct.unpack_from('<512I', s['mem'], a) for a in (0x1c879, 0x1d079, 0x1d879)]
        d5 = sum(np.count_nonzero(np.array(g) != np.array(w)) for g, w in zip(tables512(F), want5))
        print(f't{tick}: F={F} [0x6386]={s["mem"][0x6386]} E={E} S={S:#x}: 768-tables {d} of 2304 differ, 512-tables {d5} of 1536 differ')
    s = load(440)
    base = list(s['mem'][0x11f6f:0x11f72]); lvl = struct.unpack('b', s['mem'][0x11f72:0x11f73])[0]
    want = struct.unpack_from('<32I', s['mem'], 0x11eef)
    got = lut_11f73(base, lvl)
    print(f't440 0x11f73 LUT base {base} level {lvl}: {sum(g != w for g, w in zip(got, want))} of 32 differ')
    # converters: in 0x1212a (t4050) [0x12425] = 0x13d73(copy of [0x655e]); in 0x169e7 (t2515) [0x654e] = 0x13eec(copy of [0x655e])
    s = load(4050)
    planar = np.frombuffer(rdb(s, rd32(s, 0x655e), 0x30000), np.uint8)
    got = to16(planar); want = np.frombuffer(rdb(s, rd32(s, 0x12425), 0x20000), '<u2')
    print(f't4050 0x13d73([0x12425] <- [0x655e]): {np.count_nonzero(got != want)} of 65536 differ')
    s = load(2515)
    planar = np.frombuffer(rdb(s, rd32(s, 0x655e), 0x30000), np.uint8)
    got = to_rgb(planar); want = np.frombuffer(rdb(s, rd32(s, 0x654e), 0x30000), np.uint8)
    print(f't2515 0x13eec([0x654e] <- [0x655e]): {np.count_nonzero(got != want)} of 196608 differ')

def check_flash(path):
    """runs/f1365.flash.pickle (plug_flash.py): tables at the end of 0x638b's flash test / after 0x17207, per frame.
    Before the flash they are 0x17003's (E -200, S 0x1c8); from the flash on 0x1a229's (E 0, S 0x100, F)."""
    import pickle
    rec = pickle.load(open(path, 'rb'))
    flashed = False
    seq = []
    bad = 0
    for r in rec:
        if r['site'] == 0x1720c: flashed = True
        E, S = (0, 0x100) if flashed else (-200, 0x1c8)
        want = np.frombuffer(r['t768'], np.uint32)
        got = np.array(sum(tables768(E, S, r['F']), []), np.uint32)
        want5 = np.frombuffer(r['t512'], np.uint32)
        got5 = np.array(sum(tables512(r['F'] if flashed else 0), []), np.uint32)
        d = np.count_nonzero(got != want) + np.count_nonzero(got5 != want5)
        bad += d
        if flashed: seq.append(r['F'])
    print(f'{len(rec)} frames checked, {bad} table entries differ; F per frame from the flash on: {seq[:34]}')

if __name__ == '__main__' and len(sys.argv) > 1 and sys.argv[1] == '--flash':
    check_flash(sys.argv[2])
