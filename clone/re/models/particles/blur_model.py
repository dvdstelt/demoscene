"""Model of the radial blur 0x403df7 (maps) / 0x40407c (level table) / 0x404137 + 0x404278 (in-place passes).

radial_blur(mem, buf, cx, cy, level, zoom, mode, phase, dt) works on `mem`, a numpy uint8 view of linear memory
around the buffer (so reads above row 0 / below row 199 see what the original sees), and returns the new phase.
Check: python3 blur_model.py  (compares with the routine run in unicorn on snapshot buffers, every byte).
"""
import math
import sys

import numpy as np

from fpu import f32, f32t

MUL = None


def mul_table():
    """0x403f10: 64 rows of 512 signed bytes, row f, column d + 256 = (f * d) / 64 truncated toward zero."""
    t = np.zeros((64, 512), dtype=np.int64)
    for f in range(64):
        for d in range(-256, 256):
            v = f * d
            t[f, d + 256] = (abs(v) >> 6) * (1 if v >= 0 else -1)
    return t


def maps(cx, cy, zoom, mode, phase):
    """Column map (320 int32) and row map (200 int32) in 1/64 pixel: source offset of the pixel's sample."""
    col = [int((i - cx) * 64.0 * zoom) for i in range(320)]          # fild i; fsub cx; fmul 64; fmul zoom; fistp
    zy = f32t(zoom * 0.8333333333333333)                             # [ebp-0x10], stored as float
    s = math.sin(phase)
    row = []
    for j in range(200):
        t = f32t(j - cy)                                             # [ebp-8]
        v = t * 64.0 * zy
        row.append(int(v if mode == 0 else s * v))
    return col, row


def level_table(level):
    """0x40407c: byte i = (int)(i * level) & 0xff (accumulated, exact for a float32 level)."""
    return [int(i * level) & 0xff for i in range(256)]


def radial_blur(mem, buf, cx, cy, level, zoom, mode, phase, dt):
    """mem: numpy uint8 array of linear memory starting at address 0 (or a slice object with .base0);
    buf: linear address of the 8-bit 320x200 buffer. Returns the new phase ([0xeaf40c])."""
    global MUL
    if MUL is None:
        MUL = mul_table()
    phase = f32t(phase + dt * 0.04)                                  # [0xeaf40c] += 0.04 dt, every call
    col, row = maps(cx, cy, zoom, mode, phase)
    lev = level_table(level)
    icx = min(max(int(cx), 0), 319)
    icy = min(max(int(cy), 0), 199)
    m = mem
    for direction, y0 in ((-1, icy), (1, icy + 1)):
        for half in (0, 1):                                          # 0x404137: x = cx-1 .. 0; 0x404278: x = cx .. 319
            y = y0
            while 0 <= y < 200:
                rv = row[y]
                fy = (rv & 63)
                srow = buf + (y + (rv >> 6)) * 320
                xs = range(icx - 1, -1, -1) if half == 0 else range(icx, 320)
                for x in xs:
                    cv = col[x]
                    fx = cv & 63
                    s = srow + x + (cv >> 6)
                    a = int(m[s]); b = int(m[s + 320])
                    top = (a + int(MUL[fy, b - a + 256])) & 0xff
                    a2 = int(m[s + 1]); b2 = int(m[s + 321])
                    top2 = (a2 + int(MUL[fy, b2 - a2 + 256])) & 0xff
                    v = (top + int(MUL[fx, top2 - top + 256])) & 0xff
                    d = buf + y * 320 + x
                    m[d] = min(255, int(m[d]) + lev[v])
                y += direction
    return phase


def check():
    import emu
    from fpu import f32
    rng = np.random.default_rng(1)
    BUF0 = 0xeaf410
    cases = [('t11148', 0, 0.8 + 0.19 * math.sin(13 * 2.12), 0.9),     # part 1 style
             ('t3925', 1, 0.85 + 0.14 * math.sin(6 * 1.0), 2.4),        # part 6 style (mode 1)
             ('t3925', 1, 0.99, 40.0), ('t3925', 1, 0.7, 80.0)]
    for snap, mode, level, dt in cases:
        e = emu.Emu(snap)
        # an input with points, lines and noise in buffer 0 (rows 20..179)
        img = np.zeros((200, 320), dtype=np.uint8)
        for _ in range(300):
            x, y = rng.integers(0, 316), rng.integers(20, 176)
            img[y:y + 4, x:x + 4] = np.minimum(255, img[y:y + 4, x:x + 4].astype(int) + rng.integers(20, 200))
        img[100:103, 40:280] = 180
        e.wr(BUF0, img.tobytes())
        e.wrf(0x420489, dt)
        phase0 = e.rdf(0xeaf40c)
        lo = BUF0 - 0x10000
        mem = np.zeros(BUF0 + 0x20000, dtype=np.uint8)
        mem[lo:BUF0 + 0x20000] = e.buf(lo, 0x20000 + 0x10000)
        lv = f32(level)
        ph = radial_blur(mem, BUF0, 160.0, 100.0, lv, f32(-0.025), mode, phase0, f32(dt))
        e.call(0x403df7, eax=BUF0, stack=[emu.f32(160.0), emu.f32(100.0), emu.f32(lv), emu.f32(-0.025), mode])
        out = e.buf(BUF0, 64000)
        diff = np.count_nonzero(out != mem[BUF0:BUF0 + 64000])
        changed = np.count_nonzero(out != img.reshape(-1))
        print("%s mode %d level %.4f dt %.1f: phase %.6f (emu %.6f, sin %+.3f), %d bytes changed by the blur, %d differ from the model, max |d| %d" % (
            snap, mode, lv, dt, ph, e.rdf(0xeaf40c), math.sin(ph), changed, diff,
            np.abs(out.astype(int) - mem[BUF0:BUF0 + 64000]).max()))


if __name__ == '__main__':
    check()
