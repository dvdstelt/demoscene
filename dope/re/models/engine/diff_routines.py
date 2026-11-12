"""Print the differences between p2's copy of an engine routine and another part's copy (instruction by
instruction, through the recursive-descent listing). usage: diff_routines.py p3 0x1c60:0x1cd0 [...]"""
import os, re, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
import rep


def listing(v, off):
    out = []
    for ins in v.md.disasm(v.img[0x100 + off:0x100 + off + 0x1200], off):
        out.append(ins)
        if ins.mnemonic in ('ret', 'retf', 'iret') or len(out) > 600:
            break
    return out


def shape(ins):
    return ins.mnemonic + ' ' + re.sub(r'0x[0-9a-f]+|\b\d+\b', 'N', ins.op_str)


p2 = rep.View('p2')
v = rep.View(sys.argv[1])
for pair in [x for x in sys.argv[2:] if not x.startswith('--')]:
    a, b = (int(x, 16) for x in pair.split(':'))
    la, lb = listing(p2, a), listing(v, b)
    print('== p2 %04x vs %s %04x (%d / %d instructions)' % (a, sys.argv[1], b, len(la), len(lb)))
    import difflib
    show_imm = '--imm' in sys.argv
    sm = difflib.SequenceMatcher(None, [shape(x) for x in la], [shape(y) for y in lb], autojunk=False)
    for tag, i1, i2, j1, j2 in sm.get_opcodes():
        if tag == 'equal':
            if show_imm:
                for x, y in zip(la[i1:i2], lb[j1:j2]):
                    if x.op_str != y.op_str:
                        print('  %04x %-6s %-36s | %04x %-6s %s' % (x.address, x.mnemonic, x.op_str, y.address,
                                                                 y.mnemonic, y.op_str))
            continue
        print('  --- %s' % tag)
        for k in range(max(i2 - i1, j2 - j1)):
            x = la[i1 + k] if i1 + k < i2 else None
            y = lb[j1 + k] if j1 + k < j2 else None
            l = '%04x %-6s %-36s' % (x.address, x.mnemonic, x.op_str) if x else ' ' * 48
            r = '%04x %-6s %s' % (y.address, y.mnemonic, y.op_str) if y else ''
            print('  %s | %s' % (l, r))
