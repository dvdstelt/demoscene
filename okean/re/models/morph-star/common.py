"""Shared helpers for the morph and star models: the program image, the sine table, the capture, the blob line
drawer of parts 0x367e5 / 0x3c8da (exact, with the dword adds and their carries)."""
import os
import struct
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
RE = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, RE)

IMG = open(os.path.join(RE, 'unpacked', 'korso2.bin'), 'rb').read()
BASE = 0xec0


def rd32(a, signed=True):
    return struct.unpack_from('<i' if signed else '<I', IMG, BASE + a)[0]


def rdbytes(a, n):
    return IMG[BASE + a:BASE + a + n]


def sine_table():
    """the table at 0x24a3c as the title part's set-up 0x2c26f completes it: entries 0..1024 are in the file
    (a quarter wave, peak 1022), 1025..2048 mirror 1024..1, 2049..4096 = -(0..2047), 4097..5120 = 0..1023.
    T[i & 0xfff] is the sine, T[(i & 0xfff) + 1025] (the table at 0x25a40) the cosine."""
    t = [rd32(0x24a3c + 4 * i) for i in range(1025)] + [0] * (5121 - 1025)
    for j in range(1024):
        t[1025 + j] = t[1024 - j]
    for j in range(2048):
        t[2049 + j] = -t[j]
    for j in range(1024):
        t[4097 + j] = t[j]
    return t


T = sine_table()


def S(a):
    return T[a]


def C(a):
    return T[a + 1025]


def idiv(a, b):
    """x86 idiv: quotient truncated toward zero"""
    q = abs(a) // abs(b)
    return q if (a >= 0) == (b >= 0) else -q


def u32(x):
    return x & 0xffffffff


def ror(x, n):
    x &= 0xffffffff
    return ((x >> n) | (x << (32 - n))) & 0xffffffff


def rol(x, n):
    x &= 0xffffffff
    return ((x << n) | (x >> (32 - n))) & 0xffffffff


def blob_rows(dwords):
    """8 rows of two dwords (offset +0, +4) -> 8x8 byte array"""
    b = np.zeros((8, 8), np.int64)
    for r, (d0, d1) in enumerate(dwords):
        b[r, :4] = list(struct.pack('<I', d0))
        b[r, 4:] = list(struct.pack('<I', d1))
    return b


def line_positions(x1, y1, x2, y2, reject_x=False):
    """positions (x, y) of the blobs the line drawer 0x363d0 (0x3c46e with reject_x) puts down, in order.
    Implements the trivial rejects, the swap by unsigned y, the step count, the 8.8 steps, the fast path (all
    four coordinates on screen: ecx steps, no checks) and the slow path (cl steps, each blob checked)."""
    if not (y1 > 0) and y2 < 0:
        return []
    if reject_x:
        if not (x1 > 0) and x2 < 0:
            return []
        if x1 >= 0x140 and x2 > 0x140:
            return []
        if y1 >= 0xc8 and y2 > 0xc8:
            return []
    if u32(y1) >= u32(y2):
        x1, y1, x2, y2 = x2, y2, x1, y1
    dx = x2 - x1
    n1 = u32(dx + 1 if dx >= 0 else -dx)
    dy = y2 - y1
    n2 = u32(dy + 1 if dy >= 0 else -dy)
    n = n1 if n1 > n2 else n2
    n >>= 1
    if n == 0:
        return []
    sx = idiv(dx * 256, n)
    sy = idiv(dy * 256, n)
    if y1 < 0:                      # never reached (the swap puts a negative y second)
        x1 += (-y1) * sx
        y1 = 0
    ex, ey = x1 * 256, y1 * 256
    out = []
    if 0 <= y1 <= 0xbf and 0 <= y2 <= 0xbf and 0 <= x1 <= 0x137 and 0 <= x2 <= 0x137:
        for _ in range(n):
            out.append((ex >> 8, ey >> 8))
            ey += sy
            ex += sx
        return out
    cnt = n & 0xff or 256
    for _ in range(cnt):
        y = ey >> 8
        x = ex >> 8
        if 0 <= y <= 0xbf and 0 <= x <= 0x137:
            out.append((x, y))
        ey += sy
        ex += sx
    return out


def add_blobs(buf, pos, dwords):
    """buf: uint8[64000] (modified). Adds the blob (8 rows x two dwords at +0 and +4, rows 320 bytes apart) at
    every position, exactly as the 32-bit adds do (a byte overflow carries into the next byte of the same dword).
    Fast path: when no byte overflows, the plain sum; otherwise sequential dword adds."""
    if not pos:
        return buf
    blob = blob_rows(dwords)
    p = np.array(pos, np.int64)
    acc = np.zeros(64000 + 3000, np.int64)
    base = p[:, 1] * 320 + p[:, 0]
    for r in range(8):
        for c in range(8):
            if blob[r, c]:
                np.add.at(acc, base + r * 320 + c, blob[r, c])
    tot = buf.astype(np.int64) + acc[:64000]
    if tot.max() <= 255:
        buf[:] = tot.astype(np.uint8)
        return buf
    b = bytearray(buf.tobytes()) + bytearray(3000)
    for o in base.tolist():
        for r, (d0, d1) in enumerate(dwords):
            a = o + r * 320
            v = (int.from_bytes(b[a:a + 4], 'little') + d0) & 0xffffffff
            b[a:a + 4] = v.to_bytes(4, 'little')
            a += 4
            v = (int.from_bytes(b[a:a + 4], 'little') + d1) & 0xffffffff
            b[a:a + 4] = v.to_bytes(4, 'little')
    buf[:] = np.frombuffer(bytes(b[:64000]), np.uint8)
    return buf


def open_capture():
    from sheet import open_frames
    return open_frames(os.path.join(RE, 'full.frames'))


def capture_frame(cap, k):
    """(indices 200x320 uint8, palette 256x3 uint8) of record k of full.frames"""
    raw, W, H, rec, hdr, n = cap
    o = hdr + k * rec
    pal = np.frombuffer(bytes(raw[o + 8:o + 776]), np.uint8).reshape(256, 3)
    idx = np.asarray(raw[o + 776:o + 776 + W * H]).reshape(H, W)[:200, :320]
    return idx, pal


def caption(buf, text):
    """the shared printer 0x4cacc drawing one script line (core scope): centred at x = 160 - (sum of advances up
    to the next CR, the '@' included) / 2, top row 162; glyphs 38 rows high from the font picture at 0x3cec2
    (stride 320), opaque where the font byte is non-zero; advance per character from 0x4c861 plus the spacing
    [0x3ceb0], a space advances 4 (+ spacing)."""
    adv = IMG[BASE + 0x4c861:BASE + 0x4c861 + 0x100]
    wid = IMG[BASE + 0x4c8c2:BASE + 0x4c8c2 + 0x40]
    sp = rd32(0x3ceb0)

    def code(ch):
        b = ord(ch)
        return b - 0x30 + 0x7b if 0x30 <= b <= 0x39 else b

    w = 0
    for ch in text + '@':
        w += 4 if ch == ' ' else adv[code(ch)]
    pos = 0xa0 - (w >> 1) + 0xca80
    out = buf.reshape(-1)
    for ch in text:
        if ch == ' ':
            pos += 4 + sp
            continue
        b = code(ch)
        g = b - 0x61
        off = sum(wid[i] - 1 for i in range(g))
        src = BASE + 0x3cec2 + g + off
        cw = wid[g]
        for r in range(0x26):
            row = IMG[src + r * 320:src + r * 320 + cw]
            for i, v in enumerate(row):
                if v:
                    out[pos + r * 320 + i] = v
        pos += adv[b] + sp
    return buf
