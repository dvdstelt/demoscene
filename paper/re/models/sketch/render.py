"""Python model of the sketch renderer (0x5eec mode 2 / 0x43df / 0x18b7 / 0x1612 / 0x1982 / 0x1b10)."""
import numpy as np, struct
from mm import M

PAD = 1024
class R:
    def __init__(self, snap):
        self.s = snap
        s = snap
        self.T = np.frombuffer(s.b, np.uint8, 0x10000, s.d(0x1eded)).reshape(256, 256)
        self.grey = [int(v) for v in np.frombuffer(s.b, np.uint8, 0x800, s.d(0x1ee09))]   # grey table followed by jitter table
        self.jit = np.frombuffer(s.b, np.int8, 0x400, s.d(0x1ee0d))
        self.buf = np.full(64000 + 2*PAD, 0x3a, np.uint8)
        self.edge = np.zeros((256, 2), np.int64)
        self.ymin = 0xff; self.ymax = 0

    def px(self, off, g):
        o = off + PAD
        self.buf[o] = self.T[g, self.buf[o]]

    @staticmethod
    def rcp(n): return 65536 // n

    def line1612(self, eax, ebx, ecx, edx, gstart):
        gi = gstart
        G = self.grey
        edx = edx - ebx
        if edx < 0:
            edx = -edx; ebx -= edx; eax, ecx = ecx, eax
        ecx = ecx - eax
        xmaj_neg = False
        if ecx < 0:
            ecx = -ecx
            if ecx > edx:
                # 0x176b
                eax -= ecx; ebx += edx; edx = -edx
                return self.xmajor(eax, ebx, ecx, edx, gi)
            ecx = -ecx
        else:
            if ecx > edx:
                return self.xmajor(eax, ebx, ecx, edx, gi)
        # y major 0x1635
        if ebx >= 200 or edx <= 0: return
        ebp = self.rcp(edx) * ecx
        ebp = ((ebp + 2**31) % 2**32) - 2**31
        eax <<= 16
        edx = edx + ebx
        if edx <= 0: return
        if edx > 200: edx = 200
        if ebx < 0:
            eax -= ebp * ebx; ebx = 0
        edx -= ebx
        if edx <= 0: return
        if ebp >= 0:
            if eax >= 0x1400000: return
            if eax < 0:
                while True:
                    ebx += 1; eax += ebp
                    if eax >= 0: break
                    edx -= 1
                    if edx == 0: return
            if edx == 0: return
            while True:
                x = eax >> 16
                self.px(ebx*320 + x, G[gi]); self.px(ebx*320 + x + 1, G[gi+1]); gi += 2
                ebx += 1
                eax += ebp
                if eax >= 0x1400000: return
                edx -= 1
                if edx == 0: return
        else:
            if eax < 0: return
            if eax >= 0x1400000:
                while True:
                    ebx += 1; eax += ebp
                    if eax < 0x1400000: break
                    edx -= 1
                    if edx == 0: return
            if edx == 0: return
            while True:
                x = eax >> 16
                self.px(ebx*320 + x, G[gi]); self.px(ebx*320 + x + 1, G[gi+1]); gi += 2
                ebx += 1
                eax += ebp
                if eax < 0: return
                edx -= 1
                if edx == 0: return

    def xmajor(self, eax, ebx, ecx, edx, gi):
        G = self.grey
        # 0x1771: eax = x start, ebx = y start, ecx = dx > 0, edx = dy (signed)
        if eax >= 320 or ecx <= 0: return
        ebp = self.rcp(ecx) * edx
        ebx <<= 16
        ecx = ecx + eax
        if ecx > 320: ecx = 320
        if eax < 0:
            ebx -= ebp * eax; eax = 0
        ecx -= eax
        if ecx <= 0: return
        if ebp >= 0:
            if ebx >= 0xc80000: return
            if ebx < 0:
                while True:
                    eax += 1; ebx += ebp
                    if ebx >= 0: break
                    ecx -= 1
                    if ecx == 0: return
            if ecx == 0: return
            row = (ebx & 0xffffffff) >> 16
            frac = (ebx << 16) & 0xffffffff
            step = (ebp << 16) & 0xffffffff
            x = eax
            while True:
                self.px(row*320 + x, G[gi]); self.px(row*320 + x + 320, G[gi+1]); gi += 2
                x += 1
                frac += step
                if frac >= 2**32:
                    frac -= 2**32; row += 1
                    if row >= 200: return
                ecx -= 1
                if ecx == 0: return
        else:
            if ebx <= 0: return
            if ebx >= 0xc80000:
                while True:
                    eax += 1; ebx += ebp
                    if ebx < 0xc80000: break
                    ecx -= 1
                    if ecx == 0: return
            if ecx == 0: return
            row = (ebx & 0xffffffff) >> 16
            frac = (ebx << 16) & 0xffffffff
            step = (-((ebp << 16) & 0xffffffff)) & 0xffffffff
            x = eax
            while True:
                self.px(row*320 + x, G[gi]); self.px(row*320 + x + 320, G[gi+1]); gi += 2
                x += 1
                frac += step
                if frac >= 2**32:
                    frac -= 2**32; row -= 1
                    if row < 0: return
                ecx -= 1
                if ecx == 0: return

    def line(self, x0, y0, x1, y1):
        """0x18b7"""
        J = self.jit
        p0 = (x0*y0 + y0) & 0x1ff
        p1 = (x1*y1 + y1) & 0x1ff
        gstart = (x0 + y0 + x1 + y1) & 0xff
        for k in range(3):
            Y1 = int(J[p1]) + y1; X1 = int(J[p1+1]) + x1; p1 += 2
            Y0 = int(J[p0]) + y0; X0 = int(J[p0+1]) + x0; p0 += 2
            self.line1612(int(X0), int(Y0), int(X1), int(Y1), gstart)

    # polygon
    def edge1982(self, eax, ebx, ecx, edx):
        slot = 0
        if ebx == edx: return
        if ebx > edx:
            slot = 1; ebx, edx = edx, ebx; eax, ecx = ecx, eax
        if ebx >= 200 or edx <= 0: return
        dy = edx - ebx
        ebp = self.rcp(dy) * (ecx - eax)
        if edx > 200: edx = 200
        eax <<= 16
        if ebx < 0:
            eax -= ebx * ebp; ebx = 0
        if ebx < self.ymin: self.ymin = ebx
        if edx > self.ymax: self.ymax = edx
        for y in range(ebx, edx):
            self.edge[y, slot] = min(max(eax, 0), 0x1400000)
            eax += ebp

    def tri(self, pts, colour, op='set', target=None):
        self.ymin = 0xff; self.ymax = 0
        (a, b), (c, d), (e, f) = pts
        self.edge1982(a, b, c, d); self.edge1982(c, d, e, f); self.edge1982(e, f, a, b)
        buf = self.buf if target is None else target
        for y in range(self.ymin, self.ymax):
            l = self.edge[y, 1] >> 16; r = self.edge[y, 0] >> 16
            if r - l > 0:
                o = y*320 + l + (PAD if target is None else 0)
                if op == 'set': buf[o:o + r - l] = colour
                else: buf[o:o + r - l] |= colour

    def img(self): return self.buf[PAD:PAD+64000].reshape(200, 320)
