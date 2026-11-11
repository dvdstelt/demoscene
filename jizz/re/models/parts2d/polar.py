"""The polar table [0x16f4b] (precalc 0x16f4f, core scope) and the warp renderer 0x16dc8, as used by the logo part
(0x1212a) and the radial zoom (0x177bf).

table entry (600 x 340 dwords, row j, column i): x = i - 299.5, y = j - 169.5,
  byte 0 = round(atan2(y, x) / (2pi/256) * 2) & 255   (512 units per turn: the low byte repeats every half turn)
  byte 1 = round(1.3 * sqrt(x^2 + y^2)) & 255
"""
import numpy as np

import common as c

f32 = np.float32


def table():
    j, i = np.mgrid[0:340, 0:600]
    x = (i - 299.5).astype(np.float32)
    y = (j - 169.5).astype(np.float32)
    a = np.arctan2(y.astype(np.float64), x.astype(np.float64))
    a = (a / f32(0.02454369328916073)).astype(np.float32) * f32(2)
    ang = np.rint(a).astype(np.int64) & 255
    r = np.sqrt((x * x + y * y).astype(np.float32)).astype(np.float32) * f32(1.2999999523162842)
    rad = np.rint(r.astype(np.float32)).astype(np.int64) & 255
    return (ang | (rad << 8)).astype(np.uint32)


def table_from_mem(m):
    b = c.u32(m, 0x16f4b)
    return np.frombuffer(m[b:b + 600 * 340 * 4], '<u4').reshape(340, 600)


def warp(tab, offset, mod, add, tex16):
    """0x16dc8: page[y][x] = tex16[(entry + (mod[angle] << 8) + add) & 0xffff] for the 320x200 window that starts
    at byte offset `offset` in the table (2400 bytes per row). mod = 256 signed bytes (added to the radius byte),
    add = 16-bit word [0x16cd1] (low byte: angle, high byte: radius, with carry)."""
    row, col = divmod(offset // 4, 600)
    e = tab[row:row + 200, col:col + 320].astype(np.int64)
    a = e & 255
    r = ((e >> 8) + np.asarray(mod, np.int64)[a]) & 255
    idx = ((r << 8 | a) + add) & 0xffff
    return tex16[idx]
