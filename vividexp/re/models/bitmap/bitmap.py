"""Python model of VIVID.EXE's bitmap effects (InitBITMAPEFEK 0x400ae4, RunBITMAPEFEK 0x401270, the step 0x40c22c and
the drawers 0x40c3ad..0x40cbe7). Reads the extracted archive files (unpacked/files/1, unpacked/files/2).

    eff = Effect('efek/efek1.ini', part=1)        # what InitBITMAPEFEK builds
    img = eff.draw(offs)                           # 200x320 uint8 VGA indices for the eight int16 offsets
    pal = eff.palette                              # 256x3 6-bit, what Init writes to the DAC
    offs = step(offs, speeds, n)                   # 0x40c22c called n times (n = refreshes since the last draw)
    speeds = read_prm('efek/1efek1.prm', part=1)   # 0x401024
"""
import os
import re

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
RE = os.path.abspath(os.path.join(HERE, '..', '..'))
FILES = os.path.join(RE, 'unpacked', 'files')


def find(name, part):
    for d in ([str(part)] + ['1', '2', '3']):
        p = os.path.join(FILES, d, name)
        if os.path.exists(p):
            return p
    raise FileNotFoundError(name)


def read_raw(name, part):
    """Image Alchemy 'mhwanh' raw: 32-byte header, 768-byte 8-bit palette, pixels"""
    d = open(find(name, part), 'rb').read()
    assert d[:6] == b'mhwanh'
    return np.frombuffer(d[32:800], np.uint8).reshape(256, 3), np.frombuffer(d[800:], np.uint8)


def read_prm(name, part):
    """0x401024: eight words = (short)trunc(atof(s) * 256) (Watcom's 0x41130c sets the FPU to truncate)"""
    f = open(find(name, part), 'rb').read().decode('latin-1').split()[:8]
    return [int(np.trunc(float(s) * 256)) for s in f]


def step(offs, sp, n=1):
    """0x40c22c called n times with the arguments 0x4011e4 / 0x401270 pack: arg k = (sp[2k+1] << 16) + sp[2k]
    (sign-extended), so the y word gets sp[2k+1] - 1 when sp[2k] < 0. The fourth plane's words are stepped with
    the third argument (a bug), never with speeds 7 and 8."""
    o = list(offs)
    pairs = [(sp[0], sp[1]), (sp[2], sp[3]), (sp[4], sp[5]), (sp[4], sp[5])]
    for k, (sx, sy) in enumerate(pairs):
        hi = sy - (1 if sx < 0 else 0)
        o[2 * k] = (o[2 * k] + n * sx) & 0xffff
        o[2 * k + 1] = (o[2 * k + 1] + n * hi) & 0xffff
    return [v - 0x10000 if v >= 0x8000 else v for v in o]


def addoff(offs, k):
    """the 16-bit value the drawers add to a table word for plane k: high byte of the y word, high byte of the x word"""
    return (((offs[2 * k + 1] & 0xffff) >> 8) << 8) | ((offs[2 * k] & 0xffff) >> 8)


def tausta_shift(px):
    """InitBITMAPEFEK: every dword of the 64,000-byte background minus 0x3f3f3f40 (indices 0..63 -> 192..255)"""
    d = px.view('<u4').astype(np.int64)
    d = (d - 0x3f3f3f40) & 0xffffffff
    return d.astype('<u4').view(np.uint8)


class Effect:
    def __init__(self, ini, part):
        text = open(find(ini, part), 'rb').read().decode('latin-1').split()
        ints = [int(x) for x in text[:4]]
        names = text[4:4 + 15]
        self.type, self.planes, self.bpic, _ = ints
        self.bpicname, self.dtabname = names[0], names[1]
        self.pics = names[4:8]
        self.tabs = names[8:15] + ['0']
        self.part = part
        n = 64000 if self.type == 1 else 16000
        # PREC: per plane a word per pixel (1x1) or per 2x2 block: low byte from T(2p+1), high byte from T(2p+2)
        self.prec = []
        nprec = (1 if self.planes < 2 else 2) if self.type == 1 else self.planes
        for p in range(nprec):
            lo = np.frombuffer(open(find(self.tabs[2 * p], part), 'rb').read(), np.uint8)[:n].astype(np.uint16)
            hi = np.frombuffer(open(find(self.tabs[2 * p + 1], part), 'rb').read(), np.uint8)[:n].astype(np.uint16)
            self.prec.append(lo | (hi << 8))
        # PICRGB: planes of 64 KB (256 x 256 textures)
        pal8, px = read_raw(self.pics[0], part)
        self.pic = [px[:65536].copy()]
        for p in range(1, self.planes):
            self.pic.append(read_raw(self.pics[p], part)[1][:65536].copy())
        self.dtable = None
        if self.type == 1 and self.planes == 2:
            pal8, dt = read_raw(self.dtabname, part)
            self.dtable = dt[:32768].copy()
        self.palette = (pal8.astype(np.uint8) >> 2).copy()
        self.tausta = None
        if self.bpic == 1:
            bp, bpx = read_raw(self.bpicname, part)
            self.tausta = tausta_shift(bpx[:64000].copy())
            self.palette[192:256] = bp[:64] >> 2

    def draw(self, offs):
        if self.type == 1:
            idx1 = self.pic[0][(self.prec[0].astype(np.int64) + addoff(offs, 0)) & 0xffff]
            if self.planes == 2:          # 0x40cbe7: DTABLE[plane2 << 8 | plane1]
                idx2 = self.pic[1][(self.prec[1].astype(np.int64) + addoff(offs, 1)) & 0xffff]
                img = self.dtable[(idx2.astype(np.int64) << 8) | idx1]
            else:                         # 0x40cab5 / 0x40cb33
                img = idx1
            if self.tausta is not None:   # 0x40cb33: the background shows except where it holds 0xc0
                img = np.where(self.tausta == 0xc0, img, self.tausta)
            return img.reshape(200, 320).astype(np.uint8)
        # 2x2 (0x40c3ad, 0x40c543, 0x40c74f; with background 0x40c455, 0x40c626, 0x40c86d): sum of the planes' texels
        s = np.zeros(16000, np.int64)
        for p in range(self.planes):
            k = min(p, 3)
            s += self.pic[min(p, 2) if p == 3 else p][(self.prec[p].astype(np.int64) + addoff(offs, k)) & 0xffff]
        blk = (s & 0xff).astype(np.uint8).reshape(100, 160)
        img = np.repeat(np.repeat(blk, 2, axis=0), 2, axis=1)
        if self.tausta is not None:
            t = self.tausta.reshape(200, 320)
            img = np.where(t == 0xc0, img, t)
        return img
