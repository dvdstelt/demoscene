"""glow.py: models of the "glow line" pipeline shared by 0x11986 (TBL), 0x17003 and 0x169e7, checked against the
buffer dumps of runs/d1365.dump.pickle and runs/d2515.dump.pickle (made with plug_dump.py):
  0x183a9 jittered quads -> 0x1122f / 0x1145c lines (value 0xf9, 4 px thick) into the 8-bit line buffer,
  per byte >> 3 (64000 bytes), 0x11eb2 2x2 box blur, 0x12111 trail (d += (s - d) >> n), 0x11396 fade-to-C."""
import sys, struct, pickle
import numpy as np
sys.path.insert(0, '/home/dennis/src/dvdstelt/demoscene/jizz/re/models/core')
from rng import Rng
from fade import fade_tables

W = 320
def ror(x, n): return ((x >> n) | (x << (32 - n))) & 0xffffffff

def line(buf, x0, y0, x1, y1, w=320, h=200, val=0xf9):
    """0x1122f + 0x1145c on a bytearray with stride 320"""
    if not (0 <= x0 < w and 0 <= x1 < w and 0 <= y0 < h and 0 <= y1 < h): return   # unsigned compares
    if y1 < y0: x0, y0, x1, y1 = x1, y1, x0, y0
    dy = y1 - y0; dx = x1 - x0
    neg = dx < 0; dx = abs(dx)
    if dx + dy == 0: return                         # a single point draws nothing
    o = y0 * W + x0
    if dy <= dx:                                    # flat: 4 bytes high
        slope = (dy << 16) // dx
        if slope >= 0x10000: slope = 0xffff
        frac = 0
        for i in range(dx + 1):
            for k in range(4): buf[o + k * W] = val
            o += -1 if neg else 1
            frac += slope
            if frac >= 0x10000: frac -= 0x10000; o += W
    else:                                           # steep: 4 bytes wide
        slope = (dx << 16) // dy
        if slope >= 0x10000: slope = 0xffff
        frac = 0
        for i in range(dy + 1):
            for k in range(4): buf[o + k] = val
            if not neg:
                frac += slope; c = frac >> 16; frac &= 0xffff
                o += W + c
            else:
                frac -= slope; c = 1 if frac < 0 else 0; frac &= 0xffff
                o += W - c

def quads(buf, q, first, last, mask, rng):
    """0x183a9: quads of 4 corners (int32 x, int32 y) = 32 bytes; one random number per edge"""
    for n in range(first, last + 1):
        P = struct.unpack_from('<8i', q, 32 * n)
        for k in range(4):
            r = rng.next()
            k1 = (k + 1) & 3
            x0 = P[2 * k] + (r & mask); r = ror(r, 4)
            y0 = P[2 * k + 1] + (r & mask); r = ror(r, 4)
            x1 = P[2 * k1] + (r & mask); r = ror(r, 4)
            y1 = P[2 * k1 + 1] + (r & mask)
            line(buf, x0, y0, x1, y1)

def shr3(b):
    b[:64000] >>= 3

def blur(b, dst, src):
    """0x11eb2: out[i] = (s[i] + s[i+1] + s[i+320] + s[i+321]) >> 2 for 0xf500 bytes, 8-bit wrapping sums;
    the first output uses 0 for s[i] + s[i+320]; in place when src == dst (reads ahead only)"""
    al = 0; dl = 0; bl = 0
    for i in range(0xf500):
        dl = (dl - bl) & 0xff
        bl = al
        al = (int(b[src + i + 1]) + int(b[src + i + 1 + W])) & 0xff
        dl = (dl + al) & 0xff
        b[dst + i] = dl >> 2

def blur_np(b, dst, src):
    """same as blur() when no sum wraps (all values < 64): vectorised"""
    s = b.astype(np.int32)
    i = np.arange(0xf500)
    a1 = s[src + i + 1] + s[src + i + 1 + W]
    a0 = np.concatenate([[0], a1[:-1]])
    out = ((a0 + a1) & 0xff) >> 2
    if src == dst:
        # in place: reads at src+i+1.. are never written before they are read, so this equals the loop
        pass
    b[dst:dst + 0xf500] = out

def trail(b, s_off, d_off, n):
    """0x12111: 65536 bytes, d = (((d << n) & 0xff) + ((s - d) & 0xff)) & 0xff) >> n"""
    s = b[s_off:s_off + 0x10000].astype(np.int32); d = b[d_off:d_off + 0x10000].astype(np.int32)
    b[d_off:d_off + 0x10000] = ((((d << n) & 0xff) + ((s - d) & 0xff)) & 0xff) >> n

def fade_buf(page, v, tabs):
    """0x11396: pixels with v != 0 become R[v][..] | G[v][..] | B[v][..] (v = fade row 1..31 towards C)"""
    R, G, B = tabs
    p = page.astype(np.uint32)
    out = (R[v, (p & 0xf800) >> 11] | G[v, (p & 0x7c0) >> 6] | B[v, p & 0x1f]).astype(np.uint16)
    return np.where(v != 0, out, page)

if __name__ == '__main__':
    tot = 0
    for f in pickle.load(open(sys.argv[1], 'rb')):
        res = []
        lb = 0x100000      # [0x652a] in the emulator
        if 'lines_before' in f and 'lines_after' in f:
            b = np.frombuffer(f['lines_before'], np.uint8).copy()
            first, last = f['q']
            quads(b, f['quads'], first, last, f['ebp'], Rng(*f['rng']))
            res.append(('0x183a9 lines', np.count_nonzero(b != np.frombuffer(f['lines_after'], np.uint8))))
        b = np.frombuffer(f['pre_shr'], np.uint8).copy()
        shr3(b)
        nxt = f.get('pre_blur', f.get('pre_trail'))
        if 'pre_blur' in f:
            res.append(('>>3', np.count_nonzero(b != np.frombuffer(f['pre_blur'], np.uint8))))
            b = np.frombuffer(f['pre_blur'], np.uint8).copy()
            dst, src = f['blur_args'][0] - lb, f['blur_args'][0] - lb + f['blur_args'][1]
            blur(b, dst, src)
            res.append(('0x11eb2 blur', np.count_nonzero(b != np.frombuffer(f['pre_trail'], np.uint8))))
        elif 'pre_trail' in f:
            res.append(('>>3 (no blur)', np.count_nonzero(b != np.frombuffer(f['pre_trail'], np.uint8))))
        if 'pre_trail' in f:
            b = np.frombuffer(f['pre_trail'], np.uint8).copy()
            s_off, d_off, n = f['trail_args']
            trail(b, s_off - lb, d_off - lb, n)
            res.append((f'0x12111 trail n={n}', np.count_nonzero(b != np.frombuffer(f['pre_fade_buf'], np.uint8))))
        b = np.frombuffer(f['pre_fade_buf'], np.uint8)
        esi = f['fade_args'][0] - lb
        v = b[esi:esi + 64000].reshape(200, 320).astype(np.int64)
        T = np.frombuffer(f['tables'], np.uint32).reshape(3, 32, 64)
        page = np.frombuffer(f['pre_fade_page'], '<u2').reshape(200, 320)
        got = fade_buf(page, v, T)
        res.append((f'0x11396 fade (max v {v.max()})', np.count_nonzero(got != np.frombuffer(f['post_fade_page'], '<u2').reshape(200, 320))))
        tabs_ok = all(np.array_equal(T[c, :, :32], fade_tables(31, sh)[:, :32]) for c, sh in enumerate((11, 6, 0)))
        print(f"event {f['event']} tick {f['tick']}: " + ', '.join(f'{k}: {d} differ' for k, d in res) + f'; tables = C 31: {tabs_ok}')
        tot += sum(d for k, d in res)
    print('total differences', tot)
