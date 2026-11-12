"""Model of p4's "FRACTAL scape" voxel landscape (1ee8 clear + 1efc), from unpacked/p4.bin only, checked against the
landscape buffers 4f1e dumped by hook_p4.py (models/p4/out/land_<n>.npz, with [2113] read after 1efc returned).

usage (from dope/re): python3 models/p4/land_model.py
"""
import glob
import os
import struct

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
RE = os.path.dirname(os.path.dirname(HERE))
img = open(os.path.join(RE, 'unpacked', 'p4.bin'), 'rb').read()
CS = 0x100
HM = np.frombuffer(img[0x2eec0:0x2eec0 + 65536], np.uint8).astype(np.int32)   # 2eec:0000 heights 0..118
CM = np.frombuffer(img[0x3eec0:0x3eec0 + 65536], np.uint8).astype(np.int32)   # 3eec:0000 colours 1..12
DIST = [struct.unpack_from('<H', img, CS + 0x2058 + 2 * i)[0] for i in range(61)]  # 0010:2058
UPOS = struct.unpack_from('<H', img, CS + 0x2111)[0]                               # 0010:2111 = 0100h


def render(P, ybuf=None, split=None):
    """one frame: returns the 320x200 buffer (sky = 2) and the column buffer after the frame.
    split = (k, P0): rows 55..k+1 use [2113] = P0 instead of P (an IRQ advanced it while the frame was drawn)"""
    Pfinal = P
    buf = np.full(64000, 2, np.uint8)
    if ybuf is None:
        ybuf = [0x6400] * 320
    ybuf = list(ybuf)
    for i in range(55, 0, -1):                       # [2117] = 6eh .. 2 step 2 -> DIST index 55 .. 1
        P = split[1] if split and i > split[0] else Pfinal
        z = (DIST[i] << 4) + (15 - (P & 15))         # 16-bit
        base = 0x48 + 0x8000 // z
        step = z << 5                                 # [211d]
        if i == 1:
            base, scale = 0x6400, 0
        else:
            scale = 0xaaaa // z
        U = ((UPOS << 12) - (step << 7)) & 0xffffffff
        bl = (U >> 16) & 0xff
        bh = (((P << 5) & 0xffff) >> 8) + (z >> 5) & 0xff
        cx = (U & 0xffff) >> 1
        for col in range(319):                        # si = 27eh .. 2: 319 columns, column 319 is never drawn
            cx = cx * 2 + (step & 0xffff)
            carry = cx >> 16
            cx &= 0xffff
            bl = (bl + ((step >> 16) & 0xff) + carry) & 0xff
            cx >>= 1
            bx = bh * 256 + bl
            h0 = HM[bx]
            h1 = HM[(bx + 1) & 0xffff]
            diff = h1 - h0                            # ax = 9-bit signed
            prod = diff * cx                          # imul cx (cx < 8000h)
            ax = (prod >> 7) & 0xffff                 # shrd ax, dx, 7
            ax = (ax + (h0 << 8)) & 0xffff            # add ah, h0
            dxv = (ax * scale) >> 16                  # mul [211b] -> dx
            y = (base - dxv) & 0xffff
            if y & 0x8000:
                y = -1
            elif y >= 200:
                y = 199
            old = ybuf[col]
            ybuf[col] = y
            n = old - y
            if n < 0:
                c = CM[bx]
                for r in range(y, old, -1):           # rows y, y-1, ..., old+1
                    buf[r * 320 + col] = c
    return buf.reshape(200, 320), ybuf


def main():
    files = sorted(glob.glob(os.path.join(HERE, 'out', 'land_*.npz')), key=lambda p: int(p.split('_')[-1][:-4]))
    for p in files:
        d = np.load(p)
        P = int(d['pos'])
        ref = d['buf'].reshape(200, 320)
        res = []
        for dp in (0, -2, -4):
            out, yb = render((P + dp) & 0xffff, [199] * 320)
            res.append((int((out != ref).sum()), dp))
        best = min(res)
        print('%s frame %d [2113]=%d: differing pixels %d (with [2113]%+d)' % (os.path.basename(p), int(d['frame']), P,
                                                                         best[0], best[1]))




def check_split():
    """frames that differ from a single-[2113] render: find the row where [2113] changed"""
    files = sorted(glob.glob(os.path.join(HERE, 'out', 'land_*.npz')), key=lambda p: int(p.split('_')[-1][:-4]))
    for p in files:
        d = np.load(p)
        P = int(d['pos'])
        ref = d['buf'].reshape(200, 320)
        out, _ = render(P, [199] * 320)
        if (out != ref).sum() == 0:
            continue
        found = None
        for k in range(55, 0, -1):
            o2, _ = render(P, [199] * 320, split=(k, (P - 2) & 0xffff))
            if (o2 != ref).sum() == 0:
                found = k
                break
        print('%s: [2113] = %d from row index %s on (rows before: %d)' % (os.path.basename(p), P, found, P - 2))


if __name__ == '__main__':
    main()
    check_split()
