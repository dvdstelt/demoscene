"""Find p2's engine routines in p3, p5, p6, p7 by comparing instruction sequences (mnemonics + operand shapes,
ignoring immediates and displacements). usage: python3 models/engine/match_routines.py"""
import os, re, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
import rep

ROUTINES = {  # p2 address: name
    0x371e: 'frame loop (run N retraces)', 0x354a: 'object loop', 0x348b: 'per object', 0x1d86: 'matrix',
    0x1c60: 'vertex loop', 0x1b60: 'vertex rotate', 0x3377: 'face setup', 0x3585: 'radix sort',
    0x3310: 'draw loop', 0x2881: 'triangle setup', 0x2660: 'span loop (texture+env)', 0x2ea0: 'span loop (env)',
    0x3030: 'triangle setup (env)', 0x360c: 'frame copy', 0x2580: 'per-retrace', 0x2550: 'palette scale',
    0x19ce: 'sine table', 0x1998: 'blend table 2', 0x1967: 'blend table 1', 0x1a21: 'int 8 handler',
    0x1a5f: 'PIT calibration', 0x1b17: 'exit restore',
}


def shape(ins):
    op = re.sub(r'0x[0-9a-f]+', 'N', ins.op_str)
    op = re.sub(r'\b\d+\b', 'N', op)
    return ins.mnemonic + ' ' + op


def seq(v, off, n):
    out = []
    for ins in v.md.disasm(v.img[0x100 + off:0x100 + off + n * 8], off):
        out.append(shape(ins))
        if len(out) >= n:
            break
    return out


def main():
    p2 = rep.View('p2')
    parts = {p: rep.View(p) for p in ('p3', 'p5', 'p6', 'p7')}
    for a, name in sorted(ROUTINES.items()):
        ref = seq(p2, a, 24)
        row = ['%-28s p2 %04x' % (name, a)]
        for p, v in parts.items():
            best = None
            code = v.img[0x100:0x10100]
            for cand in range(0, min(0x10000, len(v.img) - 0x100)):
                if code[cand] != p2.img[0x100 + a]:
                    continue
                s = seq(v, cand, len(ref))
                k = sum(1 for x, y in zip(s, ref) if x == y)
                if best is None or k > best[0]:
                    best = (k, cand)
                    if k == len(ref):
                        break
            row.append('%s %04x(%d/%d)' % (p, best[1], best[0], len(ref)))
        print('  '.join(row), flush=True)


main()
