"""Compare the rasterizers' code (0x412264, 0x412db0, 0x414e5c, 0x41669c, 0x416dc8, 0x4172e0) instruction by
instruction after normalising addresses, to see which parts are shared templates."""
import os, re, sys, difflib
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
import rep
R = {0x412264: 0x412db0, 0x412db0: 0x4134c7, 0x414e5c: 0x415790, 0x41669c: 0x416dc8, 0x416dc8: 0x4172e0,
     0x4172e0: 0x417e28}
def norm(a, b):
    out = []
    for ins in rep.md.disasm(rep.rd(a, b - a), a):
        s = f'{ins.mnemonic} {ins.op_str}'
        s = re.sub(r'0x41[0-9a-f]{4}\b', 'ADDR', s)
        out.append(s)
    return out
base = norm(0x414e5c, 0x415790)
for a, b in R.items():
    other = norm(a, b)
    sm = difflib.SequenceMatcher(None, base, other, autojunk=False)
    print(f'{a:06x}: {len(other)} instructions, ratio to 0x414e5c {sm.ratio():.3f}')
    if len(sys.argv) > 1 and int(sys.argv[1], 16) == a:
        for op in sm.get_opcodes():
            if op[0] != 'equal':
                print(op[0], base[op[1]:op[2]], '=>', other[op[3]:op[4]])
