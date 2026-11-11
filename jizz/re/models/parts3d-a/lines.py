"""Model of the thick line drawer 0x1145c / 0x1122f (core helper) as the parts use it, checked against
p1_hooks.pkl: buffer 'pre' of frame k+1 == decay(/blur)(pre of frame k) + lines of frame k+1."""
import pickle, struct
import numpy as np
from rings import parse
from overlay import decay, blur

def line(buf, x1, y1, x2, y2, val=0xf9):
    """buf: flat uint8 array of 0x10000 bytes, pitch 320."""
    if not (0 <= x1 < 320 and 0 <= x2 < 320 and 0 <= y1 < 200 and 0 <= y2 < 200):
        return
    if y2 < y1:
        x1, y1, x2, y2 = x2, y2, x1, y1
    dy = y2 - y1; dx = x2 - x1; neg = dx < 0; dx = abs(dx)
    if dx + dy == 0:
        return
    p = y1 * 320 + x1
    if dy > dx:      # y-major: 4 px wide horizontally, one row per step
        s = (dx << 16) // dy
        if s >= 0x10000: s = 0xffffffff
        s &= 0xffff; f = 0
        for _ in range(dy + 1):
            buf[p:p + 4] = val
            if not neg:
                f += s; c = f >> 16; f &= 0xffff; p += 320 + c
            else:
                f -= s; c = 1 if f < 0 else 0; f &= 0xffff; p += 320 - c
    else:            # x-major: 4 px tall, one column per step
        s = (dy << 16) // dx
        if s >= 0x10000: s = 0xffffffff
        s &= 0xffff; f = 0
        for _ in range(dx + 1):
            for k in range(4): buf[p + 320 * k] = val
            p += -1 if neg else 1
            f += s
            if f >> 16: p += 320
            f &= 0xffff

if __name__ == '__main__':
    rec = pickle.load(open('p1_hooks.pkl', 'rb'))
    pre = {e[2]: e for e in rec['buf'] if e[0] == 'pre'}
    rings = {fl: b for t, fl, b in rec['rings']}
    for fl in sorted(pre):
        if fl + 1 not in pre or fl + 2 not in rings: continue
        e = pre[fl]
        a = decay(np.frombuffer(e[3][:0x10000], np.uint8))
        if e[4][1]: a = blur(a)
        a = a.copy()
        # segments drawn in frame fl+1 = positions stored by the 0x18009 call of that frame (seen at the next call)
        segs = [s[1] for s in parse(rings[fl + 2])]
        for x1, y1, x2, y2 in segs: line(a, x1, y1, x2, y2)
        nxt = np.frombuffer(pre[fl + 1][3][:0x10000], np.uint8)
        print('flip', fl + 1, 'pre buffer exact: %.6f' % (a[:64000] == nxt[:64000]).mean(), 'ndiff', int((a[:64000] != nxt[:64000]).sum()))
