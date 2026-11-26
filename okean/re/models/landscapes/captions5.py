"""Which caption page each drawn frame of part 5 shows (the printer model alone, with the music position at the
end of the refresh before the one that shows the frame)."""
import numpy as np
from common import *
import textprint
from framemap import framemap
T = read_ticks_full()
M = Mem(load_snap('part_38520'))
FM = framemap('38520', 0, 4693, 5751, T[4693]['irqs'])
P = textprint.Printer(M)
buf = bytearray(64000 + 0x4000)
prev = None
for j, k, n in FM:
    start = P.ptr
    o, r = T[k - 1]['order'], T[k - 1]['row']
    P.draw(buf, o, r)
    page = bytes(M.m[C32 + start:C32 + start + 40]).split(b'@')[0][1:].decode()
    if start != prev:
        print('drawn %4d  record %d (t %.3f s)  n %4d  music at printer call %d:%02d  page %#x %r' % (
            j, k, T[k]['t'], n, o, r, start, page))
        prev = start
