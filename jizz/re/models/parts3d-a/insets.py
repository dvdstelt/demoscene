"""Checks of the white inset frame (0x1151c) and the sprite blits (0x174e5) against full.frames."""
import numpy as np
from snap import mem, frame

def frame_rect(fr, x, y, w, h, col=0xffdf):
    """0x1151c: 1-pixel outline at x-1..x+w (columns) and y-1..y+h (rows); offset [0x15c3b] = y*640 + 2x."""
    n = 2 * ((w >> 1) + 1)          # rep stosd of (w >> 1) + 1 dwords from x-1
    top = fr[y - 1, x - 1:x - 1 + n]; bot = fr[y + h, x - 1:x - 1 + n]
    left = fr[y:y + h, x - 1]; right = fr[y:y + h, x + w]
    return all((a == col).all() for a in (top, bot, left, right))

def blit_ok(fr, spr, src_row, x, y, w=254, h=110):
    """0x174e5: copy w x h 16-bit pixels from the 256-wide sprite (rows src_row..), skipping value 0."""
    s = spr[src_row:src_row + h, :w]; d = fr[y:y + h, x:x + w]
    m = s != 0
    return (s[m] == d[m]).mean(), m.mean()

if __name__ == '__main__':
    m = mem(1365)
    spr = m.arr(m.u32(0x6562), 65536, '<u2').reshape(256, 256)
    print('part 1 inset 175x110 at (130,20), tick 2300:', frame_rect(frame(2300), 130, 20, 175, 110))
    print('part 1 inset 160x100 at (30,90), tick 2450:', frame_rect(frame(2450), 30, 90, 160, 100))
    print('part 2 inset 170x95 at (90,77), tick 2620:', frame_rect(frame(2620), 90, 77, 170, 95))
    print('part 2 inset 160x100 at (30,20), tick 3050:', frame_rect(frame(3050), 30, 20, 160, 100))
    print('part 2 inset 160x100 at (70,80), tick 3140:', frame_rect(frame(3140), 70, 80, 160, 100))
    print('part 2 inset 160x100 at (80,50), tick 3240:', frame_rect(frame(3240), 80, 50, 160, 100))
    print('sprite 1 (rows 0..109) at (50,30), tick 1845 (event 210): equal, coverage', blit_ok(frame(1845), spr, 0, 50, 30))
    print('sprite 2 (rows 104..213) at (2,85), tick 1965 (event 220): equal, coverage', blit_ok(frame(1965), spr, 104, 2, 85))
