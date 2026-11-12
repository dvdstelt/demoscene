"""Model of p4's "FIRE clouds" (1aab feedback, 1b36 / 1b56 tables, 1b91 composite, 1999 palette), built from
unpacked/p4.bin only, checked against the hooked run (models/p4/out/, from hook_p4.py).

Checks:
  1. the generated tables (scaled cloud 1b36, blend table 1b56, sine table 1ea8) equal the run's memory (init.npz);
  2. the fire buffer after every recompute (CRC32 in frames.log, full dumps fire_<n>.npy) equals the model stepped
     from a zero buffer with the logged offsets, and the logged offsets equal the formula from the IRQ count;
  3. composite + palette of chosen frames equal the capture full.frames (where the frame was not torn).
usage (from dope/re): python3 models/p4/fire_model.py
"""
import os
import struct
import zlib

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
RE = os.path.dirname(os.path.dirname(HERE))
OUT = os.path.join(HERE, 'out')
img = open(os.path.join(RE, 'unpacked', 'p4.bin'), 'rb').read()
CS = 0x100


def seg(s, n, off=0):
    return np.frombuffer(img[s * 16 + off:s * 16 + off + n], np.uint8)


# ---- tables ---------------------------------------------------------------------------------------------------------
def sine_table():
    """1ea8: cs:0000..03ff holds a quarter wave (512 words); mirrored to 0400..07ff, negated to 0800..0fff, and the
    first quarter copied to 1000..13ff (cosine overrun)."""
    q = np.frombuffer(img[CS:CS + 0x400], '<i2').astype(np.int32)
    half = np.concatenate([q, q[::-1]])           # entry 1023-i = q[i]... see below
    # 1ea8: di = 7fe, stores q[i] at 7fe - 2i: word 1023 - i = q[i]; words 512..1023 = q[511..0]
    half = np.concatenate([q, q[::-1]])
    full = np.concatenate([half, -half, q])
    return full                                     # 2560 words: s[i] for i = 0..2559


SINE = sine_table()
CLOUD = np.minimum((seg(0x1ec1, 65536).astype(np.int32) * 7) >> 2, 127)       # 1b36
TMAP = np.frombuffer(img[0x7d90:0x7d90 + 64000], '<u2').astype(np.int32)      # 07d9:0000, 32000 words


def blend_table(si=0x18):
    """1b56: B[r][c] = min((c*si + r*(256-si) + 0x3f) >> 8, 0x70), rows of 256 bytes (128 used)"""
    r = np.arange(128)[:, None]
    c = np.arange(128)[None, :]
    b = np.zeros((128, 256), np.int32)
    b[:, :128] = np.minimum((c * si + r * (256 - si) + 0x3f) >> 8, 0x70)
    return b


BLEND = blend_table()
WORD_OF_PIXEL = 31999 - np.arange(32000)          # fire pixel p reads map word 31999 - p


def fire_offset(phase, pos):
    """1aab: bx = (sine[phase & 0xffe] + pos) >> 8 (32-bit arithmetic shift), 16 bits"""
    s = int(SINE[(phase & 0xffe) // 2])
    return ((s + pos) >> 8) & 0xffff


def fire_step(F, off):
    """one recompute: F[p] = B[F[p]][C[(T[31999-p] + off) & 0xffff]]"""
    a = (TMAP[WORD_OF_PIXEL] + off) & 0xffff
    return BLEND[F, CLOUD[a]].astype(np.uint8)


DITHER = np.array([[7, 0, 4, 1], [1, 7, 0, 4], [4, 1, 7, 0], [0, 4, 1, 7]], np.int32)   # edx = 01040007h, rol 8/row


def composite(F, pic, label=None, lw=0):
    """1b91: screen = ((2*F[y][x>>1] + D[y&3][x&3]) & 0xf0) + pic; label (70x44, colours f0..ff) at rows 148..191:
    its first w = min(d, 70) columns from x = 319 - d (d = [196d] = 0..75; the cap applies to the width only)"""
    f = np.repeat(F.reshape(200, 160).astype(np.int32), 2, axis=1)
    y, x = np.mgrid[0:200, 0:320]
    out = (((2 * f + DITHER[y & 3, x & 3]) & 0xf0) + pic.reshape(200, 320)).astype(np.uint8)
    if label is not None and lw > 0:
        w = min(lw, 70)
        out[148:192, 319 - lw:319 - lw + w] = label[:, :w]
    return out


def scale_table(dx):
    """1975: dx < 0 -> 0; dx >>= 4; dx = min(dx, 256); t[i] = ah of ax, ax += dx while ax <= 3f00h"""
    if dx & 0x8000:
        dx = 0
    dx = min(dx >> 4, 0x100)
    t, ax = [], 0
    for _ in range(64):
        t.append(ax >> 8)
        if ax <= 0x3f00:
            ax += dx
    return np.array(t + [0] * 192, np.int32)


FIREPAL = seg(0x0010, 45, 0x15ae).reshape(15, 3).astype(np.int32)   # 0010:15ae, 15 colours (6-bit)


def palette(pal16, a):
    """1999: DAC[16*L + c] = min(Sp[pal16[c]] + Sf[FIREPAL[L]], 63), L = 0..14, c = 0..15;
    Sp = scale_table(1000h - a), Sf = scale_table(a)"""
    sp = scale_table((0x1000 - a) & 0xffff)
    sf = scale_table(a & 0xffff)
    p = np.minimum(sp[pal16.reshape(16, 3)][None, :, :] + sf[FIREPAL][:, None, :], 63)
    return p.reshape(240, 3)


def main():
    ini = np.load(os.path.join(OUT, 'init.npz'))
    print('cloud table (1b36) equal:', np.array_equal(ini['cloud'], CLOUD.astype(np.uint8)))
    print('blend table (1b56) equal:', np.array_equal(ini['blend'].reshape(128, 256), BLEND.astype(np.uint8)))
    s = np.frombuffer(ini['sine'].tobytes(), '<i2')
    print('sine table (1ea8) equal:', np.array_equal(s[:2560], SINE))
    ref = np.floor(32767 * np.sin(2 * np.pi * np.arange(512) / 2048) + 0.5).astype(int)
    q = SINE[:512]
    keep = np.arange(512) != 510
    print('quarter = round(32767 sin(2 pi i / 2048)) except entry 510:', np.array_equal(q[keep], ref[keep]),
          '(entry 510 = %d, formula %d)' % (q[510], ref[510]))
    rows = [l.split() for l in open(os.path.join(OUT, 'frames.log'))]
    F = np.zeros(32000, np.uint8)
    n = ok = bad_off = 0
    prev = None
    prev_off = struct.unpack_from('<H', img, CS + 0x1b0b)[0]   # the patched immediate in the image: 0
    for r in rows:
        frame, irq, phase, pos, off, nrec, crc = int(r[0]), int(r[2]), int(r[7]), int(r[8]), int(r[11]), int(r[12]), int(r[13], 16)
        if off == prev_off:          # 1aab returns at once when the offset did not change (the hook's own count
            continue                 # also counted the very first call, whose offset 0 equals the image's 0)
        prev_off = off
        nrec_model = n + 1
        if fire_offset(phase, pos) != off:
            bad_off += 1
        # the IRQ count gives the phase and the position: pos = -65536 * ceil(irq/2) (odd IRQs update)
        F = fire_step(F, off)
        n += 1
        ok += zlib.crc32(F.tobytes()) == crc
        p = os.path.join(OUT, 'fire_%d.npy' % nrec)
        if os.path.exists(p) and not np.array_equal(np.load(p), F):
            print('dump %d differs' % nrec)
        # the offset from the IRQ count k = [141e] (odd IRQs update): phase = step sum, pos = -65536 * ceil(k/2)
        if pos != -65536 * ((irq + 1) // 2):
            print('pos formula differs at frame', frame)
    print('recomputes %d, model equal %d, offset formula mismatches %d' % (n, ok, bad_off))


if __name__ == '__main__':
    main()
