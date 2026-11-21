"""Linear disassembly of a range, skipping bytes capstone cannot decode. usage: dump.py start end"""
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
import rep
a, end = int(sys.argv[1], 16), int(sys.argv[2], 16)
tg = rep.call_targets()
while a < end:
    ins = next(rep.md.disasm(rep.rd(a, 15), a), None)
    if ins is None:
        print(f'{a:06x}: db {rep.rd(a,1)[0]:#04x}'); a += 1; continue
    if a in tg: print(f'\n; ---- {a:06x} called from {" ".join(hex(c) for c in tg[a][:10])}')
    print(rep.annotate(ins)); a += ins.size
