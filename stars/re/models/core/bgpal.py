"""How the palette entries used by the 3D-part backgrounds (19, 25, 27, 47) change during their parts: for every
captured frame, is DAC[idx] a uniform fade of the file palette (min(63, p * L >> 8)) or a fade from white
(63 + ((p - 63) * L >> 6)), and over which frames is it full."""
import numpy as np
import cap

CASES = [(19, 15, range(1, 7), 7170, 9475), (25, 22, range(1, 7), 13810, 15971),
         (27, 26, range(0, 256), 5947, 7015), (47, 42, range(0xe6, 0xee), 19504, 21955)]
for n, pf, idx, a, b in CASES:
    used = sorted(set(np.unique((np.frombuffer(cap.overlay(n), np.uint8).astype(int) + (0xe6 if n == 47 else 0)) & 255)))
    idx = [i for i in idx if i in used]
    P = np.frombuffer(cap.overlay(pf), np.uint8).reshape(256, 3).astype(int)[idx]
    kinds = {}
    for f in range(a, b + 1):
        D = cap.palette(f).astype(int)[idx]
        k = None
        if n == 27: P0 = P.copy(); P0[0] = np.frombuffer(cap.overlay(31), np.uint8)[:3]; Q = P0
        else: Q = P
        if np.array_equal(D, Q): k = 'full'
        else:
            for L in range(0, 257):
                if np.array_equal(D, np.minimum(63, (Q * L) >> 8)): k = 'scaled'; break
                if np.array_equal(D, 63 + (((Q - 63) * min(L, 64)) >> 6)) and L <= 64: k = 'from white'; break
                if L <= 64 and np.array_equal(D, (Q * L) >> 6): k = 'scaled/64'; break
            if k is None: k = 'other'
        kinds.setdefault(k, []).append(f)
    print(f'file {n} (palette {pf}, indices {idx[0]:#x}..{idx[-1]:#x}):',
          {k: (len(v), v[0], v[-1]) for k, v in kinds.items()})
