"""Model of part 0xa0ca's per-tick state (PIT callback 0xa4ac and the frame routines 0xa3c9 / 0xa530 / 0xa656).

t = PIT ticks since the callback was installed (= [0xa725]). Compares with out/runs/tex.log_at (logged at 0xa128,
i.e. after 0xa656 and before the render): python3 model_tex.py out/runs/tex.log_at
"""
import struct
import sys

import numpy as np

import snap

I = snap.IMG
KEYS = np.frombuffer(I, np.int16, 36, 0xa082).reshape(6, 6).astype(np.int64)      # x y z a b c per key
SEQ = [struct.unpack_from('<i', I, 0x9faa + 4 * i)[0] for i in range(34)]


def trunc_div(a, b): return int(a / b) if a * b >= 0 else -(abs(a) // abs(b))


def object_state(t):
    """0xa656 with [0xa528] = key, [0xa52c] = f: values = A + trunc((B - A) * f / 338), angles in 16 bits."""
    key, f = (t // 338) % 6, t % 338
    A, B = KEYS[key], KEYS[(key + 1) % 6]
    out = []
    for i in range(6):
        d = int(B[i] - A[i])
        if i >= 3: d = (d + 0x8000 & 0xffff) - 0x8000
        v = A[i] + trunc_div(d * f, 338)
        if i >= 3: v = (v + 0x8000 & 0xffff) - 0x8000
        out.append(int(v))
    return out


def morph_state(t):
    """0x78fda per tick: key k (0..32), sub s (0..62)."""
    return (t // 63) % 33, t % 63


def tri(t, t1):
    """[0xa076]: 63 until the frame routine first sees t >= 0x437 (at t1, the tick of that frame) and sets the
    direction to -1; from then on the callback steps it by -1 / +1 per tick between 63 and 0: a triangle wave with
    period 126 ticks."""
    if t <= t1: return 63
    return abs(63 - ((t - t1) % 126))


if __name__ == '__main__':
    rows = [l.split() for l in open(sys.argv[1])]
    t1 = min(int(r[3]) for r in rows if int(r[15]) != 0)    # first logged frame with a direction
    bad = 0
    for r in rows:
        v = list(map(int, r[3:]))
        a046, a52c, a528, x, y, z, aa, bb, cc, mk, ms, a076, a07a, a725 = v[:14]
        t = a725
        st = object_state(t)
        k, s = morph_state(t)
        ok = (st == [x, y, z, aa, bb, cc]) and a076 == tri(t, t1) and (a528, a52c) == ((t // 338) % 6, t % 338) and (mk, ms) == (k, s)
        if not ok:
            bad += 1
            if bad < 10: print('mismatch', r, st, (k, s))
    print('direction first set at t1 =', t1)
    print(len(rows), 'frames,', bad, 'mismatches; [0xa725] - [0xa046] =', sorted({int(r[16]) - int(r[3]) for r in rows}))
