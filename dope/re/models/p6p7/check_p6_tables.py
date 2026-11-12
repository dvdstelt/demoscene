"""p6: check the palette morph (38ca/3871 + 3895), the bump offset table (3913) and the shadow-pattern update of the
env map (frame loop 2e05..2e66) against snapshots."""
import sys
import numpy as np
from snap import Snap, img

b = img('p6')
P1 = np.frombuffer(b, np.uint8, 768, 0x4190)               # 0419:0000 (copied to cs:325d)
P2 = np.frombuffer(b, np.uint8, 768, 0x100 + 0x3565)       # cs:3565
ENV0 = np.frombuffer(b, np.uint8, 256 * 128, 0x449 * 16).reshape(128, 256)
PAT = np.frombuffer(b, np.uint8, 32 * 4096, 0x368a * 16).reshape(32, 64, 64)


def ramp(w):            # 3871: 64 entries, entry i = (i * (w >> 4 clamped to 0..256)) >> 8
    if w < 0: w = 0
    k = min(w >> 4, 0x100)
    return np.array([(i * k) >> 8 for i in range(64)], np.int32)


def morph_dac(m):
    a, c = ramp(0x1000 - m), ramp(m)
    v = a[P1[0x30:0x30 + 0x240]] + c[P2[0x30:0x30 + 0x240]]
    return np.minimum(v, 63).astype(np.uint8)


def bump_table(k):      # 3913: byte[i] = high byte of (-8k + i*k) (16 bit), word[hi*16+lo] = byte[hi]<<8 | byte[lo]
    by = [(((-8 * k + i * k) & 0xffff) >> 8) for i in range(16)]
    return np.array([(by[h] << 8) | by[l] for h in range(16) for l in range(16)], np.uint16)


def shadow_env(a, bang):  # 2e05..2e66: pattern index from the group angles, right half of the env map
    ax = (-bang) & 0xffff
    if ((a - 0x4000) & 0x8000) == 0:
        ax = bang & 0xffff
    idx = (ax & 0x3fff) >> 9
    env = ENV0.copy()
    sel = PAT[idx].repeat(2, 0).repeat(2, 1) & 1        # 64x64 -> 128x128
    shade = (env[:, 128:].astype(np.int32) - 0x10) & 0x3f
    env[:, 128:] = shade + np.where(sel == 1, 0x50, 0x90)
    return env, idx


for path in sys.argv[1:]:
    s = Snap(path)
    m = s.cs16(0x3259)
    tA = np.frombuffer(s.rd(0x10, 0x2ed9, 64), np.uint8)
    tB = np.frombuffer(s.rd(0x10, 0x2f19, 64), np.uint8)
    # which m made the tables (the frame loop computes them; m may have moved on since)
    ms = [mm for mm in range(0, 0x1001) if (ramp(0x1000 - mm) == tA).all() and (ramp(mm) == tB).all()]
    dac = np.frombuffer(bytes(s.vga['dac']), np.uint8)
    exp = np.minimum(tA[P1[0x30:0x270]].astype(int) + tB[P2[0x30:0x270]], 63)
    print(path, 'm=[3259]=%#x tables fit m in [%s..%s]' % (m, hex(ms[0]) if ms else None, hex(ms[-1]) if ms else None),
          'DAC 16..207 matches tables:', bool((dac[0x30:0x270] == exp).all()))
    k = s.cs16(0x176e)
    tab = np.frombuffer(s.rd(0x10, 0x10, 512), np.uint16)
    ks = [kk for kk in range(0, 0x101) if (bump_table(kk) == tab).all()]
    print('   [176e]=%#x bump table fits k=%s' % (k, [hex(x) for x in ks]))
    env = np.frombuffer(s.rd(0x449 + 0, 0, 256 * 128), np.uint8).reshape(128, 256)
    a, bb = s.cs16(0x163e), s.cs16(0x1640)
    hits = []
    for back in range(0, 4):     # the frame loop may have run a few retraces before the snapshot (b += 0x80 each)
        e, idx = shadow_env(a, (bb - back * 0x80) & 0xffff)
        if (e == env).all():
            hits.append((back, idx))
    print('   env map right half = mask update from the group angles (retraces back, mask index):', hits)
