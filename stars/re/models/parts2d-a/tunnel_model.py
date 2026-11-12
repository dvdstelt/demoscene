"""Model of the tunnel 0x11e8 (tables 0x136a / 0x10d6, wiggle 0x1158 / 0x11ce, voxel columns 0x1261, blit 0x1320).

1. builds the polar map (0x136a) and the projection table (0x10d6..0x110a) and compares them with the snapshot
   (runs/tunnel_map.npy, runs/tunnel_depth.npy, written from snaps/f11498_11e8.snap);
2. renders every frame from the values the hook logged at 0x1261 (run_hook.py --hook 0x1261:cb8/8,ec6/28,f6d/4,
   4a7a0/16 --hook 0x1367:) and compares it with the record after the blit.

usage: python3 tunnel_model.py runs/tunnel [--save F,F,...]
"""
import sys

import numpy as np

from common import *

SIN = sintab()
M32 = 0xffffffff
HMAP = np.frombuffer(ofile(2), np.uint8)       # 256x256, index row*256 + column
CMAP = np.frombuffer(ofile(3), np.uint8)


def polar_map():
    """0x136a: 4096 rays from (160, 100), x -= cos, y += sin per step; first writer wins.
    word = (angle >> 4) << 8 | (190 - step), 0 = not written."""
    m = np.zeros(64000, np.int64)
    for i in range(4096):
        c, s = int(SIN[i + 1024]), int(SIN[i])
        x, y = 0xa00000, 0x640000
        cl = 190
        dl = i >> 4
        while True:
            xi = (x & M32) >> 16
            yi = (y & M32) >> 16
            xi = xi - 65536 if xi >= 32768 else xi
            yi = yi - 65536 if yi >= 32768 else yi
            k = yi * 320 + xi
            if 0 <= k < 64000 and m[k] == 0:      # (the original indexes without a check; never out of range)
                m[k] = cl | dl << 8
            x = (x - c) & M32
            if x >= 0x1400000: break
            y = (y + s) & M32
            if y >= 0xc80000: break
            cl -= 1
            if cl < 0: break
    return m


def depth_table():
    """0x10d6: row r = 0..210 (divisor r + 9), entry h = 0..255: 190 - min(189, (756 + 14 h) // (r + 9))."""
    t = np.zeros(0x10000, np.uint8)
    for r in range(211):
        h = np.arange(256)
        t[r * 256:(r + 1) * 256] = 190 - np.minimum(189, (756 + 14 * h) // (r + 9))
    return t


def wiggle(ph, f):
    """0x1158 + 0x11ce: T[b] = ((sin[(ph + b f) & 0xfff] >> 10) << 8) + b (16 bit), then D[b] = T[b] - T[b-1]."""
    T = np.zeros(256, np.int64)
    e = ph
    for b in range(256):
        e &= 0xfff
        T[b] = ((int(SIN[e]) >> 10) * 256 + b) & 0xffff
        e += f
    D = T.copy()
    D[1:] = (T[1:] - T[:-1]) & 0xffff
    return D


def columns(pos_r, pos_c, D, depth):
    """0x1261: 256 rows (angles) of up to 190 texels; returns the 256x256 buffer (cols 169..192 cleared).
    Step s reads the maps at dx (dh = row, dl = column), draws texels ch..cl-1 when cl > ch, then dx += D[s]."""
    buf = np.zeros((256, 256), np.uint8)
    dx = ((((pos_r + np.arange(256)) & 0xff) << 8) | pos_c).astype(np.int64)
    ch = np.zeros(256, np.int64)
    idx = np.arange(256)[None, :]
    for s in range(105):
        h = HMAP[dx].astype(np.int64)
        cl = depth[(2 * s) * 256 + h].astype(np.int64)
        col = CMAP[dx]
        sel = (idx >= ch[:, None]) & (idx < cl[:, None])
        buf[sel] = np.broadcast_to(col[:, None], (256, 256))[sel]
        ch = np.maximum(ch, cl)
        dx = (dx + int(D[s])) & 0xffff
    return buf


def main():
    pre = sys.argv[1]
    saves = set()
    if '--save' in sys.argv:
        saves = {int(x) for x in sys.argv[sys.argv.index('--save') + 1].split(',')}
    m = polar_map()
    snapm = np.load(os.path.join(HERE, 'runs', 'tunnel_map.npy')).astype(np.int64)
    print('polar map equal to the snapshot:', np.array_equal(m, snapm), int(np.count_nonzero(m != snapm)))
    dt = depth_table()
    snapd = np.load(os.path.join(HERE, 'runs', 'tunnel_depth.npy'))
    print('projection table equal (211 x 256 bytes):', np.array_equal(dt[:211 * 256], snapd[:211 * 256]))
    frames = Frames(pre + '.frames')
    f0 = int([l for l in open(pre + '.ticks') if not l.startswith('#')][0].split()[0])
    L = [l.split() for l in open(pre + '.hook')]
    prev = None          # the buffer persists: columns 193..255 and stale parts are never cleared
    buf_state = np.zeros((256, 256), np.uint8)
    n = ok = 0
    bad = []
    for i in range(len(L) - 1):
        if L[i][0] != '1261' or L[i + 1][0] != '1367':
            continue
        mm = L[i][11:]
        b = bytes.fromhex(mm[0])
        r8, c8, sr, sc = [int.from_bytes(b[k:k + 2], 'little') for k in (0, 2, 4, 6)]
        e = bytes.fromhex(mm[1])
        ph = int.from_bytes(e[0:4], 'little', signed=True)
        f = int.from_bytes(e[0x10:0x14], 'little', signed=True)
        D = wiggle(ph & 0xfff, f)
        buf = columns(r8 >> 8, c8 >> 8, D, dt)
        img = buf.reshape(-1)[m]
        fe = int(L[i + 1][1])
        cap = frames.vga(fe - f0).reshape(-1)
        d = int(np.count_nonzero(cap != img))
        n += 1
        ok += d == 0
        if d:
            bad.append((fe, d))
        if fe in saves:
            save('tunnel_%d_model.png' % fe, rgb(img.reshape(200, 320), frames.pal(fe - f0)), 2)
            save('tunnel_%d_capture.png' % fe, rgb(cap.reshape(200, 320), frames.pal(fe - f0)), 2)
            save('tunnel_%d_buffer.png' % fe, rgb(buf, frames.pal(fe - f0)), 2)
    print('%d frames compared, %d exact; differing %s' % (n, ok, bad[:30]))


if __name__ == '__main__':
    main()
