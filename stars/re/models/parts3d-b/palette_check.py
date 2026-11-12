"""Check the palette formulas of the three parts against the palettes in the capture (full.frames):

  0xa0ca: fade in  P20 * t >> 6 (t < 64);  morph  P22 + (((P20 - P22) * p) >> 6) (p = [0xa076], 0..63, signed product);
          fade out P20 * b >> 6, b = 63 - (t - 0x82e)
  0x164dc: 0x168a3 with levels (r, g, b) and shift s: l < 2^s: c * l >> s; else c + ((63 - c) * (l - 2^s)) >> s
  0x16f7c: fade in P42 * t >> 6, fade out P42 * b >> 7

For every logged frame the palette is computed from the logged state and compared with the capture records F + 1
and F + 2 (F = the frame number in the log; the upload follows the retrace wait)."""
import sys

import numpy as np

import snap


def F(n): return np.frombuffer(open(f'{snap.RE}/unpacked/files/{n:02d}.bin', 'rb').read(), np.uint8).astype(np.int64)


P20, P22, P42 = F(20), F(22), F(42)
PSTAR = np.frombuffer(snap.IMG, np.uint8, 768, 0x15e60).astype(np.int64)


def tex_palette(t, p, upd, lvl):
    if t < 0x40: return P20 * lvl >> 6
    if t < 0x437: return None
    if t < 0x82e:
        d = P20 - P22
        return (P22 + ((d * p) >> 6)) & 0xff
    return P20 * lvl >> 6


def star_palette(lv, sh):
    k = 1 << sh
    out = np.zeros(768, np.int64)
    for ch in range(3):
        c = PSTAR[ch::3]; l = lv[ch]
        if l < k: out[ch::3] = (c * l) >> sh
        else: out[ch::3] = (c + (((63 - c) * (l - k)) >> sh)) & 0xff
    return out


def compare(name, items):
    rows = snap.ticks()
    first = min(f for f, _ in items)
    cap = {}
    with open(f'{snap.RE}/full.frames', 'rb') as fh:
        for f, _ in items:
            for o in (1, 2):
                if f + o in cap: continue
                fh.seek(32 + (f + o) * 64768 + 64000)
                cap[f + o] = np.frombuffer(fh.read(768), np.uint8).astype(np.int64)
    n = ok = 0
    for f, pal in items:
        if pal is None: continue
        n += 1
        if any((cap[f + o] == pal).all() for o in (1, 2)): ok += 1
    print(f'{name}: {ok} of {n} computed palettes appear exactly in the capture (record F+1 or F+2)')


if __name__ == '__main__':
    tex = []
    for l in open('out/runs/tex.log_at'):
        r = list(map(int, l.split()[1:]))
        f, pit, t, p, lvl, upd = r[0], r[1], r[2], r[13], r[16], r[17]
        if upd: tex.append((f, tex_palette(t, p, upd, lvl)))
    compare('0xa0ca', tex)
    star = []
    for l in open('out/runs/star.log_at'):
        r = list(map(int, l.split()[1:]))
        f, lv, sh, upd = r[0], r[11:14], r[14], r[-1]
        if upd: star.append((f, star_palette(lv, sh)))
    compare('0x164dc', star)
    tor = []
    for l in open('out/runs/torso2.log_at'):
        r = list(map(int, l.split()[1:]))
        f, t, palflag, fade = r[0], r[2], r[-2], r[-1]
        if palflag:
            pal = P42 * fade >> 6 if t < 0x40 else (P42.copy() if t < 0xa0 else P42 * fade >> 7)
            tor.append((f, pal))
    compare('0x16f7c', tor)


def all_palettes_check():
    """Every distinct palette in the capture during the part must be one the model produces at some tick."""
    import model_star
    st = model_star.Star(); cand = set()
    while st.final != 2 or st.t < 1900:
        st.tick()
        if st.upd: cand.add(star_palette(st.lv, st.sh).tobytes())
        if st.t > 2000: break
    cand.add(star_palette([0, 0, 0], 6).tobytes())
    seen = {}; bad = 0
    with open(f'{snap.RE}/full.frames', 'rb') as fh:
        for f in range(16646, 18452):
            fh.seek(32 + f * 64768 + 64000)
            b = np.frombuffer(fh.read(768), np.uint8).astype(np.int64).tobytes()
            seen[b] = seen.get(b, 0) + 1
    for b, c in seen.items():
        if b not in cand: bad += 1
    print('0x164dc: %d distinct palettes in the capture (frames 16646..18451), %d of them not produced by the model'
          % (len(seen), bad))


if __name__ == '__main__':
    all_palettes_check()


def race_check():
    """The palettes the model does not produce: are they a mix (first colours from one tick's levels, the rest from the
    next tick's), i.e. the PIT callback changed the levels while 0x168a3 was running?"""
    import model_star
    st = model_star.Star(); seq = []
    while st.t < 1900:
        st.tick()
        if st.upd: seq.append(star_palette(st.lv, st.sh))
    cand = {p.tobytes() for p in seq}
    with open(f'{snap.RE}/full.frames', 'rb') as fh:
        for f in range(16646, 18452):
            fh.seek(32 + f * 64768 + 64000)
            b = np.frombuffer(fh.read(768), np.uint8).astype(np.int64)
            if b.tobytes() in cand: continue
            found = None
            for i in range(len(seq) - 1):
                A, B = seq[i], seq[i + 1]
                for k in range(0, 769):
                    if (b[:k] == A[:k]).all() and (b[k:] == B[k:]).all():
                        found = (i, k); break
                if found: break
            print('frame %d: %s' % (f, 'mix of consecutive model palettes, switching at byte %d (colour %d)' % (
                found[1], found[1] // 3) if found else 'not explained'))
            

if __name__ == '__main__':
    race_check()


def distinct_check(name, f0, f1, cands):
    cand = {c.tobytes() for c in cands}
    seen = {}
    with open(f'{snap.RE}/full.frames', 'rb') as fh:
        for f in range(f0, f1):
            fh.seek(32 + f * 64768 + 64000)
            b = np.frombuffer(fh.read(768), np.uint8).astype(np.int64).tobytes()
            seen.setdefault(b, f)
    bad = [f for b, f in seen.items() if b not in cand]
    print('%s: %d distinct palettes in frames %d..%d, not produced by the formulas: %s' % (name, len(seen), f0, f1 - 1,
                                                                                       bad or 'none'))


if __name__ == '__main__':
    c = [P20 * l >> 6 for l in range(64)] + [(P22 + (((P20 - P22) * p) >> 6)) & 0xff for p in range(64)]
    distinct_check('0xa0ca', 13811, 15972, c)
    c = [P42 * l >> 6 for l in range(64)] + [P42] + [P42 * b >> 7 for b in range(128)]
    distinct_check('0x16f7c', 19505, 21956, c)
