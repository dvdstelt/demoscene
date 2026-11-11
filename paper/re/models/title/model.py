"""Python model of the title / the end / logo routines, checked against snapshot memory."""
import struct, sys
import numpy as np
sys.path.insert(0, '/home/dennis/Downloads/acme_303/paper/re')
import rep
SEG = rep.SEG
S = np.array([struct.unpack_from('<i', SEG, 0x22479 + 4*i)[0] for i in range(1024)]) if any(SEG[0x22479:0x22479+16]) else None
if S is None:
    import math
    # generated at init: fsin of k*2pi/1024 *65536, fistp (round to nearest)
    S = np.array([int(round(math.sin(k * 0.0061359231515425647) * 65536)) for k in range(1024)])

def blur_pass(src, dst_shape=None):
    pass

def font():
    """0xe2e: RLE at 0x18ed7 -> 29 glyphs: (width, 32x60 mask 0/255)"""
    p = 0x18ed7
    glyphs = []
    for g in range(29):
        w = SEG[p]; p += 1
        m = np.zeros((32, 60), np.uint8)
        for r in range(32):
            x = 0
            while True:
                b = SEG[p]; p += 1
                if b == 0: break
                if b == 0xff:
                    skip, run = SEG[p], SEG[p+1]; p += 2
                else:
                    skip, run = b & 15, b >> 4
                x += skip
                for _ in range(run if run else 256):
                    if x < 60: m[r, x] = 255
                    x += 1
        glyphs.append((w, m))
    return glyphs, p

def k4(img, pad_rows=True):
    """one pass of 0xf90 on a 2D array with linear (row-wrapping) x neighbours; rows outside = given padding"""
    pass

def tex():
    """0x10d3"""
    src = SEG[0x16ca3:0x16ca3 + 2048]
    t = np.zeros((256, 256), np.uint8)
    for r in range(64):
        row = []
        for b in src[r*32:(r+1)*32]:
            hi, lo = b >> 4, b & 15
            row += [hi*16, hi*16, lo*16, lo*16]
        row = np.array(row, np.uint8)
        full = np.concatenate([row, row[::-1]])
        t[2*r] = full; t[2*r+1] = full
    t[128:] = t[:128][::-1, ::-1]
    for it in range(3):
        tmp = np.zeros((512, 260), np.int32)
        ext = np.concatenate([t, t[:, :4]], axis=1)
        tmp[:256] = ext; tmp[256:] = ext
        flat = tmp.reshape(-1)
        n = 260 * 260
        # pass 1: from flat[0:n] (with neighbours) into temp
        def pass_(a, start, count, pitch, before):
            out = np.zeros(count, np.int32)
            idx = np.arange(start, start + count)
            get = lambda k: np.where((k >= 0) & (k < len(a)), a[np.clip(k, 0, len(a)-1)], before)
            out = (get(idx - pitch) + get(idx + pitch) + get(idx - 1) + get(idx + 1)) >> 2
            return out
        p1 = pass_(flat, 0, n, 260, 0)   # row -1 is outside the buffer (unknown), only feeds unused rows
        tmp2 = np.concatenate([np.zeros(640, np.int32), p1, np.zeros(640, np.int32)])
        p2 = pass_(tmp2, 640, n, 260, 0)
        res = p2.reshape(260, 260)
        t = res[2:258, 2:258].astype(np.uint8)
    return t

def shade_tables():
    t1 = np.zeros((65, 256), np.uint8); t2 = np.zeros((65, 256), np.uint8)
    for ch in range(65):
        for cl in range(256):
            v = (cl * ch) >> 5
            t2[ch, cl] = 255 if v > 255 else v
        c = ch
        if c > 32:
            c -= 32; c = (c >> 1) + (c >> 3)
        for cl in range(256):
            if cl < 64:
                v = cl + 2*c - 64
                t1[ch, cl] = max(v, 0)
            else:
                v = (cl & 31) + c - 32
                t1[ch, cl] = max(v, 0) + (cl & 0xe0)
    return t1, t2

def palette(angle, sh):
    s = S[angle & 1023] >> sh
    c = S[(angle + 256) & 1023] >> sh
    R = (128 - s) & 255
    G0 = (128 + (s >> 1)) & 255
    cc = (c * 222) >> 8
    G = (G0 + cc) & 255; B = (G0 - cc) & 255
    pal = np.zeros((256, 3), np.int32)
    for i in range(128):
        pal[i] = [(R*i) >> 9, (G*i) >> 9, (B*i) >> 9]
    for k in range(128):
        ch = 128 - k
        pal[128+k] = [((R*ch) >> 9) + (k >> 1), ((G*ch) >> 9) + (k >> 1), ((B*ch) >> 9) + (k >> 1)]
    return pal, (R, G, B)

def fade(pal, level):
    L = level >> 8
    if L <= 0x80:
        return (pal * L) >> 7
    L = min(L, 255) - 128
    return (((pal * (127 - L)) >> 6) + L) >> 1

def f90(a, start, count, pitch):
    """one pass of 0xf90 over flat int array a: out[k] = (a[s-p] + a[s+p] + a[s-1] + a[s+1]) >> 2"""
    idx = np.arange(start, start + count)
    return (a[idx - pitch] + a[idx + pitch] + a[idx - 1] + a[idx + 1]) >> 2

def f47(flat, start, rows, pitch):
    """0xf47 in place on flat (np int array with valid memory around start): two passes, temp has 640 zero bytes margin"""
    n = rows * pitch
    p1 = f90(flat, start, n, pitch)
    tmp = np.concatenate([np.zeros(640, np.int64), p1, np.zeros(640, np.int64)])
    p2 = f90(tmp, 640, n, pitch)
    flat[start:start + n] = p2

def sprites(glyphs):
    out = []
    for c in range(26):
        w, m = glyphs[c]
        buf = np.zeros(0xc80 + 2*80, np.int64)   # one guard row each side (only for the model)
        o = 80
        b = buf[o:o+0xc80].reshape(40, 80)
        b[4:36, 20:20+w] = np.minimum(255, b[4:36, 20:20+w] + m[:, :w])
        f47(buf, o + 80, 38, 80)
        out.append(buf[o:o+0xc80].reshape(40, 80).astype(np.uint8))
    return out

def i32(v):
    v &= 0xffffffff
    return v - (1 << 32) if v & 0x80000000 else v

def quad(p0, p1, p2, n):
    step = (65536 // n) >> 7
    B = i32(4*p1 - p2 - 3*p0)
    A = i32(2*p1 - 2*p0 - B)
    out = []
    for k in range(n + 1):
        t = k * step
        c = i32(B * t)
        s_ = i32(i32(A * t) * t) >> 8
        c = i32(c + s_) >> 8
        c = i32(c + 2*p0) >> 1
        out.append(c)
    return out

def osc_point(osc, k0):
    x = y = 0
    k = k0
    for cl in (4, 3, 2, 1):
        for axis in (0, 1):
            ph, amp, spd = osc[k]; k += 1
            idx = (i32(ph << cl) >> 7) & 1023
            v = (i32(S[idx] * amp) >> 16) >> cl
            if axis == 0: x += v
            else: y += v
    return x, y

def warp_grid(osc, shx=7, shy=5):
    P = [[osc_point(osc, 24*i + 8*j) for j in range(3)] for i in range(3)]
    cx = [quad(P[i][0][0], P[i][1][0], P[i][2][0], 40) for i in range(3)]
    cy = [quad(P[i][0][1], P[i][1][1], P[i][2][1], 40) for i in range(3)]
    X = np.zeros((26, 41), np.int64); Y = np.zeros((26, 41), np.int64)
    for c in range(41):
        X[:, c] = quad(cx[0][c], cx[1][c], cx[2][c], 25)
        Y[:, c] = quad(cy[0][c], cy[1][c], cy[2][c], 25)
    return P, X, Y, (Y << shy), (X << shx)

def render_blocks(Ag, Bg, tex, table_row):
    out = np.zeros((200, 320), np.uint8)
    for br in range(25):
        for bc in range(40):
            A00, A01, A10, A11 = Ag[br, bc], Ag[br, bc+1], Ag[br+1, bc], Ag[br+1, bc+1]
            B00, B01, B10, B11 = Bg[br, bc], Bg[br, bc+1], Bg[br+1, bc], Bg[br+1, bc+1]
            dAx = (A01 - A00) >> 3; ddA = ((A11 - A10) - (A01 - A00)) >> 6; dAy = (A10 - A00) >> 3
            dBx = (B01 - B00) >> 3; ddB = ((B11 - B10) - (B01 - B00)) >> 6; dBy = (B10 - B00) >> 3
            for py in range(8):
                Al = A00 + py*dAy; Bl = B00 + py*dBy
                sa = dAx + py*ddA; sb = dBx + py*ddB
                for px in range(8):
                    a = Al + px*sa; b = Bl + px*sb
                    out[br*8+py, bc*8+px] = table_row[tex[(a >> 8) & 255, (b >> 8) & 255]]
    return out

def u32(v): return v & 0xffffffff

def logo_buffer(glyphs):
    """0xfb2 second half: 640x42 buffer (row -1 .. 40), returns flat array with base offset 640"""
    buf = np.zeros(0x6900, np.int64)
    base = 0x280
    pen = base + 0xfbb
    for ch in b'psychic link':
        if ch < 0x61:
            pen += 10
            continue
        w, m = glyphs[ch - 0x61]
        for r in range(32):
            seg = buf[pen + r*640: pen + r*640 + w]
            seg[:] = np.minimum(255, seg + m[r, :w])
        pen += w + 1
    for (x, y, w, h) in [(0xb0, 1, 0x120, 0x26), (0xb4, 4, 0xc4, 0x20)]:
        for r in range(h):
            p = base + (y + r) * 640 + x
            buf[p:p+w] = 255 - buf[p:p+w]
    f47(buf, base, 40, 640)
    return buf, base

def zoom(logo, lbase, s):
    """0x11c0 into a 202-row (row -1..200) cleared buffer, returns flat with base 320"""
    out = np.zeros(202*320, np.int64); ob = 320
    step = 0x1000000 // s
    x0 = u32(step * -160 + 0x1400000)
    sp = lbase + (x0 >> 16); fx0 = u32(x0 << 16)
    ip = i32(step) >> 16; fstep = u32(step << 16)
    if s < 0x500:
        e = i32(s * -20)
        row0 = u32(e + 0x6400) >> 8
        rows = ((-e) >> 7) & 0xff
        dp = ob + row0 * 320
        fy = 0
    else:
        dp = ob; rows = 200
        y0 = i32(step * -100 + 0x140000)
        fy = u32(y0 << 16); sp += (y0 >> 16) * 640
    for r in range(rows):
        p, fx = sp, fx0
        for x in range(320):
            out[dp + x] = logo[p]
            fx += fstep
            p += ip + (fx >> 32); fx &= 0xffffffff
        dp += 320
        fy += fstep
        if fy >> 32: sp += 640
        fy &= 0xffffffff
        sp += ip * 640
    return out, ob

def logo_frame(logo, lbase, b1996, s):
    s = max(s, 0x80)
    dh = 0xff if s >= 0x100 else ((s - 0x80) << 1) & 0xff
    T = (np.arange(256) * dh) >> 8
    z, ob = zoom(logo, lbase, s)
    comp = z.copy()
    comp[ob:ob+64000] = np.minimum(255, b1996 + T[z[ob:ob+64000]])
    vga = f90(comp, ob, 64000, 320)
    return comp[ob:ob+64000], vga

def b1996(glyphs):
    """0xfb2 first half: buffer of 202 rows (row -1..200) x 320, base 320; '1996' via 0xedc (x3, y7)"""
    buf = np.zeros(0xfc80, np.int64); base = 0x140
    pen = 0xc82
    for ch in (0x7b, 0x7c, 0x7c, 0x7d):
        w, m = glyphs[ch - 0x61]
        for r in range(25):
            for x in range(w):
                v = m[r, x]
                for k in range(7):
                    p = pen + r*7*320 + k*320 + 3*x
                    buf[p:p+4] = [v, v, v, 0]
        pen += 3*w + 2
    return buf, base

def blur1996(buf, base, n):
    for _ in range(n):
        f47(buf, base, 200, 320)
