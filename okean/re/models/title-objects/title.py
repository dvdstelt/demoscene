"""Model of the first part 0x2c26f (title): noise, beams of 8x8 stamps, the title mask 0x1502c, the fade.

frame(c, seed) -> (indices 200x320, new seed) for the draw that uses callback count c.
"""
from common import *

def sine_table():
    """the 4096(+1024)-entry sine table as the title's set-up 0x2c26f leaves it (dwords at 0x24a3c)"""
    q = fi32(0x24a3c, 0x401).astype(np.int64)        # file: entries 0..0x400 (a quarter wave)
    T = np.zeros(0x1401 + 0x400, np.int64)
    T[:0x401] = q
    for k in range(0x400):                             # 0x2c26f: [0x25a40+4k] = [0x25a3c-4k]
        T[0x401 + k] = T[0x400 - k]
    for k in range(0x800):                             # 0x2c28a: [0x26a40+4k] = -[0x24a3c+4k]
        T[0x801 + k] = -T[k]
    for k in range(0x400):                             # 0x2c2ad: [0x28a40+4k] = [0x24a3c+4k]
        T[0x1001 + k] = T[k]
    return T[:0x1401]

T = sine_table()
C = T[0x401:]          # the cosine view 0x25a40

MASK_FILE = np.frombuffer(frd(0x1502c, 64000), np.uint8)

def sar(a, n):
    return a >> n      # numpy int64 >> is arithmetic

def tdiv(a, b):
    """idiv: truncation toward zero"""
    q = np.abs(a) // abs(b)
    return np.where((a < 0) != (b < 0), -q, q)

def noise(seed):
    """16000 dwords: d = seed & 0x07070707; seed = ror(seed + 0x9d, 11) ^ 0x9d"""
    out = np.empty(16000, np.uint32)
    x = seed
    for i in range(16000):
        out[i] = x & 0x07070707
        x = (x + 0x9d) & 0xffffffff
        x = ((x >> 11) | (x << 21)) & 0xffffffff
        x ^= 0x9d
    return out.view(np.uint8).copy(), x

STAMP = np.ones((8, 8), np.int64)
STAMP[0, 0] = STAMP[0, 7] = STAMP[7, 0] = STAMP[7, 7] = 0

def points(c):
    p = c & 0xfff
    q = (0xc00 + 4 * c) & 0xfff
    R = int(T[q]) + 0x400
    idx = (p + 16 * np.arange(256)) & 0xfff
    x = sar(sar(3 * T[idx], 1) * R, 11)
    y = sar(C[idx] * R, 11)
    return x, y

def line_steps(x1, y1, x2, y2, clipped):
    """the endpoints/steps of 0x2bc7c / 0x2bf4f: list of (x, y) stamp origins (before the bounds tests)"""
    if (y1 & 0xffffffff) >= (y2 & 0xffffffff):      # 'jb' unsigned: swap unless y1 < y2 unsigned
        x1, y1, x2, y2 = x2, y2, x1, y1
    dx = x2 - x1
    n = (dx + 1) if dx >= 0 else -dx
    dy = y2 - y1
    m = (dy + 1) if dy >= 0 else -dy
    if not (n > m):    # 'ja': unsigned compare of the two
        n = m
    n = (n & 0xffffffff) >> 2
    n -= 1
    if n <= 0:
        return None
    bx = int(tdiv(np.int64((x2 - x1) << 8), n))
    bp = int(tdiv(np.int64((y2 - y1) << 8), n))
    if y1 < 0:
        x1 += (-y1) * bx
        y1 = 0
    return x1, y1, x2, y2, n, bx, bp

def s32(v):
    v &= 0xffffffff
    return v - (1 << 32) if v >= 1 << 31 else v

def beam_outer(acc, x1, y1, x2, y2):
    """0x2bc7c: stamps from one end, sorted by y; stops at the first stamp outside 0..311 x 0..191 (slow path)"""
    r = line_steps(x1, y1, x2, y2, True)
    if r is None:
        return 0
    x1, y1, x2, y2, n, bx, bp = r
    ex, ey = s32(x1 << 8), s32(y1 << 8)
    k = 0
    for _ in range(n):
        yy, xx = ey >> 8, ex >> 8
        if yy < 0 or yy > 0xbf or xx < 0 or xx > 0x137:
            break
        acc[yy:yy + 8, xx:xx + 8] += STAMP
        k += 1
        ey = s32(ey + bp); ex = s32(ex + bx)
    return k

def beam_inner(acc, x1, y1, mask):
    """0x2bf4f: from (x1, y1) to (160, 100), skipping stamps outside 0..311 x 0..191 or on the mask"""
    r = line_steps(x1, y1, 0xa0, 0x64, True)
    if r is None:
        return 0
    x1, y1, x2, y2, n, bx, bp = r
    ex, ey = s32(x1 << 8), s32(y1 << 8)
    k = 0
    for _ in range(n):
        yy, xx = ey >> 8, ex >> 8
        if not (yy < 0 or yy > 0xbf or xx < 0 or xx > 0x137) and mask(yy * 320 + xx) == 0:
            acc[yy:yy + 8, xx:xx + 8] += STAMP
            k += 1
        ey = s32(ey + bp); ex = s32(ex + bx)
    return k

def default_mask(off):
    if 0 <= off < 64000:
        return MASK_FILE[off]
    return None

def frame(c, seed, mask_rd=None, log=None):
    """indices of the frame drawn with callback count c; returns (idx, seed after, info)"""
    mask_rd = mask_rd or default_mask
    nz, seed2 = noise(seed)
    acc = np.zeros((200 + 8, 320 + 8), np.int64)
    x, y = points(c)
    X1 = tdiv(x << 7, 1024) + 160
    Y1 = tdiv(y << 7, 1024) + 100
    X2 = tdiv(x << 7, 16) + 160
    Y2 = tdiv(y << 7, 16) + 100
    outside = []
    for i in range(256):
        off = int(Y1[i]) * 320 + int(X1[i])
        m = mask_rd(off)
        if m is None:
            outside.append(i)
            m = 0
        if m == 0:
            beam_outer(acc, int(X1[i]), int(Y1[i]), int(X2[i]), int(Y2[i]))
        beam_inner(acc, int(X1[i]), int(Y1[i]), lambda o: MASK_FILE[o])
    idx = (nz.reshape(200, 320).astype(np.int64) + acc[:200, :320]) & 0xff
    return idx.astype(np.uint8), seed2, outside

def snap_mask_reader(s):
    """reads outside the mask from the part's start snapshot (player code and data before 0x1502c, the variables
    and the sine table after it); inside, the mask"""
    m = s['mem']
    def rd(off):
        if 0 <= off < 64000:
            return MASK_FILE[off]
        return int(m[C32 + 0x1502c + off])
    return rd

def seed_list(n, seed=0x303b):
    """the noise seed at the start of draws 0..n (16,000 generator steps per draw)"""
    out = [seed]
    for i in range(n):
        _, seed = noise(seed)
        out.append(seed)
    return out
