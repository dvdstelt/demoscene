"""Check the vertex morph 0x78ffe (A + ((B - A) * W[sub]) >> shift, per 16-bit component, 6 words per vertex) against
dumps: python3 morph_check.py DUMP OBJ FILE   (e.g. out/runs/star_dump400.pkl 0x16360 51)."""
import pickle
import struct
import sys

import numpy as np

import obj24
import snap


def morph(keys, seq, W, shift, k, s):
    A = keys[seq[k]][:, :6].astype(np.int64)
    B = keys[seq[k + 1]][:, :6].astype(np.int64)
    d = (B - A + 0x8000 & 0xffff) - 0x8000                  # 16-bit subtraction
    p = d * W[s]                                            # imul bp: 32-bit product dx:ax
    v = (p >> shift) + A                                    # shrd ax, dx, shift (low 16 bits kept)
    return (v + 0x8000 & 0xffff) - 0x8000


if __name__ == '__main__':
    d = pickle.load(open(sys.argv[1], 'rb'))
    m = snap.Mem(d['mem'])
    obj = int(sys.argv[2], 16)
    _, nv, nf, v0, f0, keys = obj24.load_b(int(sys.argv[3]))
    anim = m.u32(obj + 0x16)
    k, s = m.u32(anim), m.u32(anim + 4)
    shift, per = m.u8(anim + 8), m.u8(anim + 9)
    base = m.u32(obj + 0x12)                                # file + 4
    nkeys = (len(keys))
    seq = []
    a = anim + 0xa
    while True:
        p = m.u32(a)
        if p == 0xffffffff: break
        seq.append((p - (base + 4 + nv * 16 + nf * 16)) // (nv * 16)); a += 4
    W = m.arr(0x782d0, 128, np.int16).astype(np.int64)
    cur = m.arr(base + 4, nv * 8, np.int16).reshape(nv, 8)[:, :6].astype(np.int64)
    print('anim state key', k, 'sub', s, 'shift', shift, 'ticks/key', per, 'sequence', seq)
    best = None
    order = [(k * per + s - j) % ((len(seq) - 1) * per) for j in range((len(seq) - 1) * per)]   # nearest first
    for kk, ss in [(o // per, o % per) for o in order]:
        if True:
            e = morph(keys, seq, W, shift, kk, ss)
            err = np.abs(e - cur).max()
            if best is None or err < best[0]: best = (err, kk, ss)
            if err == 0: break
    print('vertex buffer equals the morph at key %d sub %d (max error %d); the logged state is key %d sub %d'
          % (best[1], best[2], best[0], k, s))
