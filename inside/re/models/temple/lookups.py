"""Pixel lookups of the putki rasterizer 0x416dc8 and the lit rasterizer 0x414e5c, checked on every pixel of the
frames run_temple.py --pixframes logged (code hooks at 0x417110 / 0x4154cb give ECX = level << 8 | low byte):
  0x416dc8: new = flare.shd[(32 + level) * 256 + old]      (low byte = the pixel that was there)
  0x414e5c: new = temple.int[level * 256 + texel]          (low byte = the texel)
usage: lookups.py out/t2.pkl"""
import sys, collections
import dumpread as D
from tables import shd, tint

for path in sys.argv[1:]:
    for rec in [r for r in D.load(path) if 'lv' in r]:
        fn = {f: k for f, kk, k in rec['faces']}
        writes = collections.defaultdict(list)
        for f, off, size, val, old in rec['pix']:
            writes[(f, off)].append((val, old[0]))
        ok = collections.Counter(); tot = collections.Counter()
        for f, addr, off, ecx, acc in rec['lv']:
            val, old = writes[(f, off)].pop(0)
            lv, lo = (ecx >> 8) & 255, ecx & 255
            good = (lo == old and shd[32 + lv, old] == val) if addr == 0x417110 else tint[lv, lo] == val
            ok[hex(fn[f])] += good; tot[hex(fn[f])] += 1
        print(path, rec['frame'], {k: '%d of %d pixels' % (ok[k], tot[k]) for k in tot})
