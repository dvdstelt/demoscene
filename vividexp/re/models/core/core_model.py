"""Python model of VIVID.EXE's core drawing and palette routines, replayed against a capture.

Ports (notes-core.md has the pseudo-C):
  0x401884 InitPCX     PCX decode into a slot (no bytes-per-line, no xmin/ymin; palette from the file if flag == 0)
  0x401c2c PutPCXSYNC  rectangle copy into VGA with modes 0 / 1 / 2 / 10..16, then the wait
  0x401ed0 fade step   DAC <- current palette >> 16, then n steps of the 16.16 deltas
  0x4020b8 InitFADE    deltas = ((p2 - p1) << 16) / steps (C division, truncation toward zero)
  0x402054 LoadPAL, 0x401fbc set all, 0x402784 ClearSCR
  0x402460 / 0x402494 / 0x40ccb8 / 0x402644 the zoom credits (x87 extended precision, truncating conversions)

usage: python3 models/core/core_model.py full.frames full.script FIRST LAST
  replays the script lines of part 1 from the capture's .script (the frame each line ran at) and compares the model's
  VGA memory and DAC with the capture's records FIRST..LAST (only records in core-drawn sections are compared).
"""
import os
import struct
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
RE = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, RE)
import files                                                   # noqa: E402
from sheet import open_frames, indexed                         # noqa: E402

LD = np.longdouble if os.environ.get("ZOOM_PC") == "64" else np.float64   # x87 precision: the start-up 0x4210ad loads CW 0x127f (53 bits)
IMG = open(os.path.join(RE, 'unpacked', 'vivid_img.bin'), 'rb').read()
BASE = 0x400000


def lookup(name):
    """0x40241c: archives 1, 2, 3 in turn; the last match wins; None if absent."""
    hit = None
    for t in files.TABLES:
        for n, a, s in files.entries(IMG, t):
            if n == name:
                hit = IMG[a - BASE:a - BASE + s]
    return hit


def trunc(x):
    """Watcom's float -> int: 0x41130c sets RC = chop, frndint, fistp."""
    return int(np.trunc(x))


class Core:
    def __init__(self):
        self.vga = np.zeros(64000, np.uint8)
        self.cur = np.zeros((256, 3), np.int64)          # 16.16
        self.delta = np.zeros((256, 3), np.int64)
        self.count = self.steps = self.first = self.last = 0
        self.active = 0
        self.dac = np.zeros((256, 3), np.uint8)
        self.slots = {}
        self.width = 0                                    # [0x826ab4]: ONE global for all slots
        self.height = 0
        self.zoom = None
        self.zbuf = None

    # ---- palette ----
    def fade_step(self, n):
        """0x401ed0: write the DAC from the current palette, then step the fade n times."""
        self.dac = ((self.cur >> 16) & 0x3f).astype(np.uint8)
        for _ in range(n):
            if self.count < self.steps:
                self.count += 1
            else:
                self.active = 0
            if self.active == 1:
                k1 = self.last - self.first + 1          # the loop bound (correct only for first == 0)
                for k in range(self.first, k1):
                    self.cur[k] += self.delta[k]

    def load_pal(self, name):
        p = np.frombuffer(lookup(name), np.uint8).reshape(256, 3).astype(np.int64)
        self.cur = p << 16
        self.fade_step(1)

    def set_all(self, r, g, b):
        self.cur[:] = np.array([r, g, b], np.int64) << 16
        self.fade_step(1)

    def init_fade(self, n1, n2, steps, first, last):
        p1 = np.frombuffer(lookup(n1), np.uint8).reshape(256, 3).astype(np.int64)
        p2 = np.frombuffer(lookup(n2), np.uint8).reshape(256, 3).astype(np.int64)
        self.first, self.last = first, last
        for k in range(first, last - first + 1):
            d = (p2[k] - p1[k]) << 16
            self.delta[k] = np.sign(d) * (np.abs(d) // steps)   # idiv: truncation toward zero
            self.cur[k] = p1[k] << 16
        self.steps, self.count, self.active = steps, 0, 1

    # ---- pictures ----
    def init_pcx(self, slot, name, flag):
        d = lookup(name)
        w = struct.unpack_from('<H', d, 8)[0] + 1
        h = struct.unpack_from('<H', d, 10)[0] + 1
        self.width, self.height = w, h
        pic = np.zeros((w + 2) * (h + 2), np.uint8)
        pos, i = 128, 0
        while i < w * h:
            b = d[pos]
            pos += 1
            if b & 0xc0 == 0xc0:
                n = b & 0x3f
                pic[i:i + n] = d[pos]
                pos += 1
                i += n
            else:
                pic[i] = b
                i += 1
        self.slots[slot] = pic
        if flag == 0:
            pal = np.frombuffer(d[pos + 1:pos + 769], np.uint8).reshape(256, 3).astype(np.int64)
            self.cur = (pal >> 2) << 16
            self.fade_step(1)

    def put_pcx(self, slot, dx, dy, sx, sy, w, h, mode):
        if dx + w > 320:
            w -= dx + w - 320
        if dy + h > 200:
            h -= dy + h - 200
        if h <= 0:
            return
        pic = self.slots[slot]
        for r in range(h):
            s = (sy + r) * self.width + sx
            src = pic[s:s + w].astype(np.uint32)
            o = (dy + r) * 320 + dx
            dst = self.vga[o:o + w]
            if mode == 0:
                dst[:] = src
            elif mode == 1:
                m = src != 0
                dst[m] = src[m]
            elif mode == 2:
                dst[:] = (dst + src) & 0xff
            elif 10 <= mode <= 16:
                dst[:] = (dst.astype(np.uint32) + ((src << (mode - 9)) & 0xff)) & 0xff

    def clear(self):
        self.vga[:] = 0

    # ---- zoom ----
    def init_zoom(self, name):
        self.zbuf = np.zeros(65536, np.uint8)
        self.init_pcx(0, name, 1)          # the flag is an unparsed %d: stack garbage, non-zero in the capture

    def zoom_frame(self, a, b, zoom):
        """0x402494 + 0x40ccb8: one frame of the zoom into the buffer, rows b..199-b copied to VGA."""
        a, z = LD(np.float32(a)), LD(np.float32(zoom))
        inv = LD(1) / z
        x0 = trunc(LD(160) - (a * LD(160) * LD(2)) * inv)
        w = (160 - x0) * 2
        rw = LD(1280) / LD(w)
        y0 = trunc(LD(100) - (a * LD(100) * LD(0.5)) * inv)
        h = (100 - y0) * 2
        rh = LD(200) / LD(h)
        du = trunc(rw * LD(65536))
        dv = trunc(rh * LD(65536))
        u0 = v0 = 0
        if x0 < 0:
            u0 = (-x0 * du) >> 16
            x0, w = 0, 320
        if b > y0:
            v0 = ((b - y0) * dv) >> 16
            y0 = b
            h = 200 - 2 * b
        buf = self.zbuf
        buf[:64000] = self.slots[0][:64000]
        src = self.slots[1]
        V = (v0 << 16) & 0xffffffff
        for r in range(h):
            row = (V >> 16) * 1280
            us = (((u0 << 16) + du * np.arange(w, dtype=np.int64)) & 0xffffffff) >> 16
            p = src[row + us]
            o = (y0 + r) * 320 + x0
            seg = buf[o:o + w]
            m = p != 0
            seg[m] = p[m]
            V = (V + dv) & 0xffffffff
        self.vga[b * 320:(200 - b) * 320] = buf[b * 320:(200 - b) * 320]
        return x0, y0, w, h, u0, v0, du, dv


VGA_HZ = 25175000 / (800 * 449)
RETRACE_LINE = 412          # run_vivid.py: the retrace starts at line 412 of 449 (line 0 = first displayed line)


def read_script(path, part=1):
    """[(period, line-in-period, line number, words)]: the period is the logged frame (record index)."""
    out = []
    for line in open(path):
        if line.startswith('#'):
            continue
        f = line.split(None, 4)
        if len(f) < 5 or int(f[2]) != part:
            continue
        ph = float(f[1]) * VGA_HZ
        out.append((int(f[0]), (ph - int(ph)) * 449, int(f[3]), f[4].split()))
    return out


def replay(script, last_record, on_record, zoom_lag=None):
    """Run the script lines at the times the capture logged them (before or after the retrace of their refresh
    period); at each retrace a waiting command steps the fade (and the zoom draws its next frame), as the wait loops
    do; on_record(k, core, waiting) is called at the end of every period (record k).
    zoom_lag(k, z) -> bool: whether the zoom frame drawn after this retrace reaches VGA before record k (the
    emulator's CPU budget), default True."""
    c = Core()
    c.set_all(0, 0, 0)
    i = 0
    waiting = None
    pending_zoom = None

    def run_line(k, t):
        nonlocal waiting
        cmd, args = t[2], t[3:]
        waiting = None
        if cmd == 'LoadPAL':
            c.load_pal(args[0])
        elif cmd == 'InitPCX':
            c.init_pcx(int(args[0]), args[1], int(args[2]))
        elif cmd == 'DeinitPCX':
            c.slots.pop(int(args[0]), None)
        elif cmd == 'InitFADE':
            c.init_fade(args[0], args[1], int(args[2]), int(args[3]), int(args[4]))
        elif cmd == 'PutPCXSYNC':
            n = [int(x) for x in args[:8]]
            c.put_pcx(n[0], *n[1:])
            if t[0] != 'xxx':
                waiting = ('wait', k)
        elif cmd == 'WaitSYNC':
            waiting = ('wait', k)
        elif cmd == 'ClearSCR':
            c.clear()
        elif cmd == 'InitZOOM':
            c.init_zoom(args[0])
        elif cmd == 'RunZOOMSYNC':
            c.init_pcx(1, args[0], 1)
            zs = {'z': float(np.float32(args[1])), 'a': float(args[2]), 'f3': float(args[3]), 'b': int(args[4])}
            c.zoom_frame(zs['a'], zs['b'], zs['z'])
            waiting = ('zoom', k, zs)
        elif cmd == 'DeinitZOOM':
            c.slots.pop(0, None)
        else:
            waiting = ('other', k)          # an effect: the model does not draw it

    for k in range(last_record + 1):
        if pending_zoom is not None:        # a zoom frame drawn after the previous retrace that was late
            c.vga[:] = pending_zoom
            pending_zoom = None
        while i < len(script) and script[i][0] == k and script[i][1] < RETRACE_LINE - 0.5:
            run_line(k, script[i][3])
            i += 1
        if waiting is not None:             # the retrace of period k
            ends = i < len(script) and script[i][0] == k
            if waiting[0] == 'zoom':
                zs = waiting[2]
                zs['z'] = max(zs['z'] + zs['f3'], 1.0)
                if c.active == 1:
                    c.fade_step(1)
                if not ends:
                    before = c.vga.copy()
                    c.zoom_frame(zs['a'], zs['b'], zs['z'])
                    if zoom_lag is not None and not zoom_lag(k, zs['z']):
                        pending_zoom = c.vga.copy()
                        c.vga[:] = before
            elif waiting[0] == 'wait' and c.active == 1:
                c.fade_step(1)
        while i < len(script) and script[i][0] == k:
            run_line(k, script[i][3])
            i += 1
        on_record(k, c, waiting)
    return c


def main():
    frames, script_path, first, last = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4])
    part = 1
    raw, n = open_frames(frames)
    last = min(last, n - 1)
    script = read_script(script_path, part)
    stats = {'vga_ok': 0, 'vga_bad': 0, 'dac_ok': 0, 'dac_bad': 0}
    bad = []

    def on_record(k, c, waiting):
        if k < first or (waiting is not None and waiting[0] == 'other'):
            return
        px, pal = indexed(raw, k)
        v = int((px.reshape(-1) != c.vga).sum())
        d = int((pal != c.dac).sum())
        stats['vga_ok' if v == 0 else 'vga_bad'] += 1
        stats['dac_ok' if d == 0 else 'dac_bad'] += 1
        if (v or d) and len(bad) < 40:
            bad.append((k, v, d, int(np.abs(pal.astype(int) - c.dac.astype(int)).max())))

    replay(script, last, on_record)
    print(stats)
    for b in bad:
        print('record %d: %d pixels differ, %d DAC values differ (max |diff| %d)' % b)


if __name__ == '__main__':
    main()
