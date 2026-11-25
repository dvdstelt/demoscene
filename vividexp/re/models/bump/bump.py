"""Model of Vivid Experiment's bump mapping (InitBUMP 0x402ec4, RunBUMPEFEK 0x403434, the drawers 0x40cdc8,
0x40ce79, 0x40cf55). notes-bump.md describes it; this file is the executable version.

    import bump
    fx = bump.Bump('bump/bump1.ini', pcx_flag=1)      # the script's InitBUMP line
    st = bump.State()                                  # RunBUMPEFEK's variables (B/A persist between effects!)
    st.step(n, fx)                                     # n screen refreshes passed (0x40349e loop)
    screen = fx.frame(st)                              # 200x320 uint8 indices, as 0x4031a4 draws them

Files are read from vividexp/re/unpacked/files/{2,3}/.
"""
import math
import os
import struct

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
RE = os.path.dirname(os.path.dirname(HERE))
FILES = [os.path.join(RE, 'unpacked', 'files', k) for k in ('2', '3', '1')]
W, H = 320, 200


def load(name):
    for d in FILES:
        p = os.path.join(d, name)
        if os.path.exists(p):
            return open(p, 'rb').read()
    raise FileNotFoundError(name)


def pcx(name):
    """0x401884: width = xmax + 1, height = ymax + 1 (header bytes 8..11), RLE stream of width*height bytes from
    offset 128 (bytes per line is not read), then 0x0c and the 768-byte palette (8-bit components; >> 2)."""
    d = load(name)
    w = struct.unpack_from('<H', d, 8)[0] + 1
    h = struct.unpack_from('<H', d, 10)[0] + 1
    out = bytearray()
    i = 128
    while len(out) < w * h:
        b = d[i]; i += 1
        if b & 0xc0 == 0xc0:
            out += bytes([d[i]]) * (b & 0x3f); i += 1
        else:
            out.append(b)
    pal = np.frombuffer(d[i + 1:i + 1 + 768], np.uint8).reshape(256, 3) >> 2
    return np.frombuffer(bytes(out[:w * h]), np.uint8).reshape(h, w), pal, d[i]


def parse_ini(name):
    """InitBUMP's sscanf "%d %d %f %f %f %f %s %s %s %s %s"."""
    t = load(name).decode('latin1').split()
    return dict(type=int(t[0]), add=int(t[1]) & 0xff, amp_x=float(t[2]), amp_y=float(t[3]),
                speed_x=float(t[4]), speed_y=float(t[5]), light=t[6], prm=t[7], wave=t[8], height=t[9],
                tab=t[10])


def f32(x):
    return float(np.float32(x))


class Bump:
    def __init__(self, ini, pcx_flag=1):
        self.ini = p = parse_ini(ini)
        self.type = p['type']
        # height picture: InitPCX slot 63, 64000 bytes into [0x82d4c8]
        self.height, self.height_pal, _ = pcx(p['height'])
        assert self.height.shape == (H, W)
        # bumptab.val (0x402948): 320 bytes per column, 200 bytes per row
        tab = np.frombuffer(load(p['tab']), np.uint8)
        self.tab_x = tab[:320].copy()
        self.tab_y = tab[320:520].copy()
        # light picture (0x4029dc mode 0): 256x256, the first 65536 bytes into [0x82d4d8]
        self.light, self.light_pal, _ = pcx(p['light'])
        assert self.light.shape == (256, 256)
        self.pcx_flag = pcx_flag                 # 0: InitPCX sets the palette from the light picture
        # bumpprm1.val: 40-byte header, then 2048 x 2 bytes
        prm = load(p['prm'])
        hdr = struct.unpack_from('<6i4f', prm, 0)
        self.prm_hdr = hdr
        self.cx0, self.cy0, self.cx1, self.cy1 = hdr[0], hdr[1], hdr[2], hdr[3]
        self.speed_b, self.speed_a = hdr[7], hdr[9]       # [0x82d49c] = 0.9, [0x82d4a0] = 1.2
        t2 = np.frombuffer(prm[0x28:0x28 + 0x1000], np.uint8).reshape(2048, 2)
        self.ring0 = t2[:, 0].copy()
        self.ring1 = t2[:, 1].copy()
        if self.type != 1:
            # 0x402ad0 twice; its first call clamps the centres to 4..316 / 4..196 before the second reads them
            src = np.frombuffer(load(p['wave']), '<u2').reshape(H, W)
            self.wave0 = self.wave_plane(src, self.cx0, self.cy0)
            cx1 = min(max(self.cx1, 4), 316)
            cy1 = min(max(self.cy1, 4), 196)
            self.wave1 = self.wave_plane(src, cx1, cy1)
        else:
            self.normals = self.static_normals()

    @staticmethod
    def wave_plane(src, cx, cy):
        """0x402ad0: one 16-bit plane of the wave buffer [0x82d4cc], the quarter field src mirrored about (cx, cy)."""
        i = np.arange(W)
        j = np.arange(H)
        sx = np.where(i < cx, 320 - cx + i, 319 + cx - i)
        sy = np.where(j < cy, 200 - cy + j, 199 + cy - j)
        return src[sy[:, None], sx[None, :]]

    def static_normals(self):
        """0x402dd8 (type 1): per pixel (u, v) = (tab_x[x] + h[x-1] - h[x+1], tab_y[y] + h[y-1] - h[y+1]) mod 256
        for x = 2..317, y = 2..197."""
        h = self.height.astype(np.int32)
        u = np.zeros((H, W), np.int32)
        v = np.zeros((H, W), np.int32)
        u[2:198, 2:318] = self.tab_x[None, 2:318] + h[2:198, 1:317] - h[2:198, 3:319]
        v[2:198, 2:318] = self.tab_y[2:198, None] + h[1:197, 2:318] - h[3:199, 2:318]
        return (u & 255).astype(np.uint8), (v & 255).astype(np.uint8)

    def surface(self, b, a):
        """0x40cf55 (type 0): water level from the two ring waves against the height picture.
        returns (level 200x320, flag 0 / 0x80)."""
        i0 = (self.wave0.astype(np.int32) - b) & 2047
        i1 = (self.wave1.astype(np.int32) - a) & 2047
        lev = (self.ring0[i0].astype(np.int32) + self.ring1[i1] + self.ini['add']) & 255
        h = self.height
        water = lev > h
        return np.where(water, lev, h).astype(np.uint8), np.where(water, 0, 0x80).astype(np.uint8)

    def frame(self, st, prev=None):
        lx, ly = st.light(self)
        out = np.zeros((H, W), np.uint8) if prev is None else prev.copy()
        if self.type == 1:
            u, v = self.normals
            flag = np.zeros((H, W), np.uint8)
            uu = (u[2:198, 2:318].astype(np.int32) + lx) & 255
            vv = (v[2:198, 2:318].astype(np.int32) + ly) & 255
        else:
            lev, flag = self.surface(st.b_word, st.a_word)
            h = lev.astype(np.int32)
            uu = (self.tab_x[None, 2:318].astype(np.int32) - h[2:198, 3:319] + h[2:198, 1:317] + lx) & 255
            vv = (self.tab_y[2:198, None].astype(np.int32) - h[3:199, 2:318] + h[1:197, 2:318] + ly) & 255
        out[2:198, 2:318] = (self.light[vv, uu].astype(np.int32) + flag[2:198, 2:318]) & 255
        return out


class State:
    """RunBUMPEFEK's animated variables. b/a ([0x82d494] / [0x82d498]) are not reset by InitBUMP: they carry on
    from the previous bump effect. ang_x / ang_y ([0x82d4a8] / [0x82d4ac]) start at 0 in InitBUMP."""
    def __init__(self, b=0.0, a=0.0):
        self.b = f32(b)
        self.a = f32(a)
        self.ang_x = 0.0
        self.ang_y = 0.0
        self.b_word = 0
        self.a_word = 0

    def step(self, n, fx):
        """0x40349e: n refreshes; b, a stay in the FPU (extended precision) and are stored as floats at the end;
        the angles are stored as floats every step."""
        db = f32(0.9) * 8          # computed in the FPU from the float [0x82d49c] and the int [0x821e74] = 8
        da = f32(1.2) * 8
        b, a = self.b, self.a
        for _ in range(n):
            b += db
            a += da
            self.b_word = rnd(b) & 0xffff
            self.a_word = rnd(a) & 0xffff
            self.ang_x = f32(self.ang_x + fx.ini['speed_x'])
            self.ang_y = f32(self.ang_y + fx.ini['speed_y'])
        if n > 0:
            self.b, self.a = f32(b), f32(a)

    def light(self, fx):
        """0x4031a4: lx = (byte) round(amp_x * sin(0.01745 * ang_x)), ly = (byte) round(amp_y * cos(0.01745 * ang_y))"""
        k = 0.01745
        lx = rnd(fx.ini['amp_x'] * math.sin(self.ang_x * k)) & 255
        ly = rnd(fx.ini['amp_y'] * math.cos(self.ang_y * k)) & 255
        return lx, ly


def rnd(x):
    """0x41130c (Watcom's __CHP): frndint with the rounding control set to truncate, then fistp: toward zero"""
    return int(math.trunc(x))


# ---------------------------------------------------------------------------------------------------------------
# A continuous version (what notes-bump.md suggests for the remake): every lookup evaluated at continuous
# coordinates in original-pixel units, no integer steps. Not a reproduction: a sketch to check the suggestions.

def bilinear(img, x, y, wrap=False):
    """img sampled at continuous (x, y) (pixel centres at integers)."""
    h, w = img.shape
    x0 = np.floor(x).astype(np.int64)
    y0 = np.floor(y).astype(np.int64)
    fx, fy = x - x0, y - y0
    if wrap:
        xs0, xs1, ys0, ys1 = x0 % w, (x0 + 1) % w, y0 % h, (y0 + 1) % h
    else:
        xs0, xs1 = np.clip(x0, 0, w - 1), np.clip(x0 + 1, 0, w - 1)
        ys0, ys1 = np.clip(y0, 0, h - 1), np.clip(y0 + 1, 0, h - 1)
    a = img[ys0, xs0] * (1 - fx) + img[ys0, xs1] * fx
    b = img[ys1, xs0] * (1 - fx) + img[ys1, xs1] * fx
    return a * (1 - fy) + b * fy


def frame_hd(fx, n_refresh, b_refresh, out_w=1280, out_h=800):
    """RGB (0..1) at out_w x out_h. n_refresh: refreshes since the Run start (light angles); b_refresh: refreshes
    the wave has run (b = 7.2 * b_refresh, a = 9.6 * b_refresh)."""
    sx, sy = out_w / W, out_h / H
    X = (np.arange(out_w) + 0.5) / sx - 0.5
    Y = (np.arange(out_h) + 0.5) / sy - 0.5
    X, Y = np.meshgrid(X, Y)
    hpic = fx.height.astype(np.float64)
    ini = fx.ini
    lx = ini['amp_x'] * math.sin(0.01745 * ini['speed_x'] * n_refresh)
    ly = ini['amp_y'] * math.cos(0.01745 * ini['speed_y'] * n_refresh)
    amp0, speed0, amp1, speed1 = fx.prm_hdr[6:10]      # 11, 0.9, 16, 1.2
    n0, n1 = fx.prm_hdr[4], fx.prm_hdr[5]               # 9, 4 periods per 2048
    b = speed0 * 8 * b_refresh
    a = speed1 * 8 * b_refresh
    cx1 = min(max(fx.cx1, 4), 316)
    cy1 = min(max(fx.cy1, 4), 196)

    def field(x, y):
        h = bilinear(hpic, x, y)
        if fx.type == 1:
            return h, np.zeros_like(h, bool)
        r0 = np.hypot(np.abs(x - fx.cx0 + 0.5) + 0.5, np.abs(y - fx.cy0 + 0.5) + 0.5)
        r1 = np.hypot(np.abs(x - cx1 + 0.5) + 0.5, np.abs(y - cy1 + 0.5) + 0.5)
        lev = (amp0 + amp0 * np.sin(2 * np.pi * n0 * (8 * r0 - b) / 2048)
               + amp1 + amp1 * np.cos(2 * np.pi * n1 * (8 * r1 - a) / 2048) + ini['add'])
        water = lev > h
        return np.where(water, lev, h), ~water

    Hc, land = field(X, Y)
    du = field(X - 1, Y)[0] - field(X + 1, Y)[0]
    dv = field(X, Y - 1)[0] - field(X, Y + 1)[0]
    tx = np.interp(X, np.arange(320), fx.tab_x.astype(np.float64))
    ty = np.interp(Y, np.arange(200), fx.tab_y.astype(np.float64))
    u = tx + du + lx
    v = ty + dv + ly
    if fx.type == 1:
        pal = fx.light_pal.astype(np.float64) / 63
        rgbpic = pal[fx.light]
        col = np.stack([bilinear(rgbpic[:, :, c], u, v, wrap=True) for c in range(3)], -1)
    else:
        L = bilinear(fx.light.astype(np.float64), u, v, wrap=True)
        pal = (np.frombuffer(load('pal/phong.pal' if 'phong.pcx' in ini['light'] else 'pal/phong2.pal'),
                             np.uint8).reshape(256, 3).astype(np.float64) / 63)
        base = np.where(land, 128, 0)
        i0 = np.clip(np.floor(L), 0, 127).astype(int)
        f = np.clip(L - i0, 0, 1)[..., None]
        i1 = np.minimum(i0 + 1, 127)
        col = pal[base + i0] * (1 - f) + pal[base + i1] * f
    border = (X < 1.5) | (X > 317.5) | (Y < 1.5) | (Y > 197.5)
    col[border] = 0
    return col
