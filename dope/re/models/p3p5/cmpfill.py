"""Compare p3's filler (1ff0..2643) with p5's bump filler (1b30..) instruction by instruction (mnemonics, operands
with addresses masked)."""
import sys, os, re
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
import rep
def lst(part, a, b):
    v = rep.View(part)
    out = []
    for ins in v.md.disasm(v.img[0x100 + a:0x100 + b], a):
        out.append((ins.address, ins.mnemonic, re.sub(r'0x[0-9a-f]+', 'N', ins.op_str), ins.op_str))
    return out
p3 = lst('p3', 0x1ff0, 0x2644)
p5 = lst('p5', 0x1b30, 0x1b30 + 0x654)
n = 0
for x, y in zip(p3, p5):
    if x[1:3] != y[1:3]:
        print('%04x %-6s %-30s | %04x %-6s %s' % (x[0], x[1], x[3], y[0], y[1], y[3])); n += 1
print('differences', n, 'of', min(len(p3), len(p5)))
