"""List the texel-address shifts (shld ebx, ecx, k before 'and bx, 7f7f') and env-map shifts in each engine part."""
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
import rep
for p in ('p2', 'p3', 'p5', 'p6', 'p7'):
    v = rep.View(p)
    code = v.img[0x100:0x10100]
    out = []
    for ins in v.md.disasm(code, 0):
        pass
    i = 0
    while True:
        i = code.find(b'\x66\x0f\xa4', i)          # shld r/m32, r32, imm8
        if i < 0:
            break
        ins = next(v.md.disasm(code[i:i + 8], i), None)
        if ins and ins.mnemonic == 'shld':
            nxt = next(v.md.disasm(code[i + ins.size:i + ins.size + 8], i + ins.size), None)
            out.append('%04x %s %s -> %s %s' % (i, ins.mnemonic, ins.op_str, nxt.mnemonic if nxt else '?', nxt.op_str if nxt else ''))
        i += 1
    print('==', p)
    for o in out:
        print('  ', o)
