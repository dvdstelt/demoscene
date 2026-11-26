"""Model of the shared text printer 0x4cacc as the landscape parts use it (the core scope describes it in full):
script pointer [0x4c8e4], pen [0x4c8dc] (offset into the buffer), page order [0x3ceb8], page row bit [0x3cebe]."""
from common import *

FONT = 0x3cec2       # 320-wide font strip, 38 rows; glyph i starts at column sum(widths[:i])
WID = 0x4c8c2        # glyph widths, 'a'..
ADV = 0x4c861        # advance per character code


class Printer:
    def __init__(self, M):
        self.M = M
        self.ptr = M.u32(0x4c8e4)
        self.pen = M.u32(0x4c8dc)
        self.order = M.u8(0x3ceb8)
        self.bit = M.u32(0x3cebe)
        self.spacing = M.i32(0x3ceb0)
        self.lines = M.u32(0x4c8e0)

    def b(self, a):
        return self.M.u8(a)

    def draw(self, buf, order, row):
        save = self.ptr
        while True:
            c = self.b(self.ptr)
            if c == 0x40:                              # '@': end of the page
                self.ptr += 1
                if order == self.order:
                    if (row & 0x10) != self.bit:
                        self.bit = row & 0x10          # next page from the next call on
                        return
                    self.ptr = save                    # same page again next time
                    return
                self.order = order
                self.bit = row & 0x10
                return
            if c == 0x0d:                              # new line: centre what follows (up to the next '\r')
                self.ptr += 1
                p, w = self.ptr, 0
                while True:
                    d = self.b(p)
                    if d == 0x0d:
                        break
                    if d == 0x20:
                        w += 4
                    else:
                        if 0x30 <= d <= 0x39:
                            d = d - 0x30 + 0x7b
                        w += self.b(ADV + d)
                    p += 1
                self.lines += 1
                self.pen = ((0xa0 - (w >> 1)) + 0xca80) & 0xffffffff
                continue
            if c == 0:
                raise RuntimeError('printer would hang')
            if c == 0x20:
                self.pen += 4 + self.spacing
                self.ptr += 1
                continue
            if 0x30 <= c <= 0x39:
                c = c - 0x30 + 0x7b
            adv = self.b(ADV + c)
            i = c - 0x61
            w = self.b(WID + i)
            gx = FONT + i + sum(self.b(WID + k) - 1 for k in range(i))
            for yy in range(0x26):
                for xx in range(w if w else 256):
                    v = self.b(gx + yy * 0x140 + xx)
                    if v:
                        buf[self.pen + yy * 0x140 + xx] = v
            self.pen += adv + self.spacing
            self.ptr += 1
