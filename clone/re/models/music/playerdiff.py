"""Routine-by-routine comparison of the two MXMPlay builds: CLONE.EXE's "cubic tiny gus xm player v1.4" (linear
0x4018d8..0x40266c in unpacked/clone_img.bin) and JIZZ.EXE's (0x296dc..0x2a316 in jizz/re/jizz_img.bin).

Both listings are normalised before the diff: addresses and jump targets are dropped, JIZZ's data-block offsets are
mapped to CLONE's ([ebp/esi + 0x26..0x59] - 1, [ebp/esi + 0x5a..] - 2: CLONE's block lacks JIZZ's key-off flag at
+0x59 and starts the header copy at +0x58), and the position-independent table loads (call / pop / add reg, k)
are reduced to "table". What remains are the real differences (notes-music.md section 4.12).

usage: python3 playerdiff.py [routine-name ...]      (default: all; 'list' prints the pairs)
"""
import difflib
import os
import re
import sys

import capstone

HERE = os.path.dirname(os.path.abspath(__file__))
RE = os.path.normpath(os.path.join(HERE, '..', '..'))
CLONE = open(os.path.join(RE, 'unpacked', 'clone_img.bin'), 'rb').read()
JIZZ = open(os.path.join(RE, '..', '..', 'jizz', 're', 'jizz_img.bin'), 'rb').read()
CBASE = 0x400000

# name: (JIZZ start, JIZZ end, CLONE start, CLONE end)
PAIRS = [
    ('datablock', 0x296dc, 0x296e7, 0x4018d8, 0x4018e3),
    ('tables', 0x296e7, 0x29738, 0x4018e3, 0x40196a),
    ('xmpInit', 0x29738, 0x297a8, 0x40196a, 0x4019ee),
    ('freq', 0x297a8, 0x29801, 0x4019ee, 0x401a47),
    ('newnote', 0x29801, 0x299ae, 0x401a47, 0x401bde),
    ('setvolume', 0x299ae, 0x299b9, 0x401bde, 0x401be9),
    ('getsync', 0x299b9, 0x299c4, 0x401be9, 0x401bf4),
    ('getpos', 0x299c4, 0x299d4, 0x401bf4, 0x401c02),
    ('clamp', 0x299d4, 0x299f1, 0x401c02, 0x401c21),
    ('effects', 0x299f1, 0x29b10, 0x401c21, 0x401e88),
    ('dispatch', 0x29b10, 0x29b26, 0x401e88, 0x401e9e),
    ('tick', 0x29b26, 0x29db1, 0x401e9e, 0x402129),
    ('xmpPlay', 0x29db1, 0x29e22, 0x402129, 0x40219a),
    ('xmpStop', 0x29e22, 0x29e34, 0x40219a, 0x4021ac),
    ('irq0', 0x29e38, 0x29ed1, 0x4021ac, 0x402245),
    ('install', 0x29ed1, 0x29f2a, 0x402245, 0x40229d),
    ('remove', 0x29f2a, 0x29f44, 0x40229d, 0x4022b3),
    ('getTimer', 0x29f44, 0x29f63, 0x4022b3, 0x4022d2),
    ('portio', 0x29f63, 0x2a02a, 0x4022d2, 0x402399),
    ('gf1reset', 0x2a02a, 0x2a0b6, 0x402399, 0x402425),
    ('setvol', 0x2a0b6, 0x2a11c, 0x402425, 0x40248b),
    ('voiceoff', 0x2a11c, 0x2a13c, 0x40248b, 0x4024ab),
    ('gusupdate', 0x2a13c, 0x2a23f, 0x4024ab, 0x4025ae),
    ('gusstartstop', 0x2a23f, 0x2a26f, 0x4025ae, 0x4025de),
    ('upload', 0x2a26f, 0x2a2cd, 0x4025de, 0x402621),
    ('ultrasnd', 0x2a2cd, 0x2a317, 0x402621, 0x40266d),
]

md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)


def jmap(m):
    v = int(m.group(2), 16)
    if 0x26 <= v <= 0x59:
        v -= 1
    elif v >= 0x5a:
        v -= 2
    return '%s0x%x' % (m.group(1), v)


def norm(img, base, a, b, jizz):
    out = []
    for ins in md.disasm(img[a - base:b - base], a):
        op = ins.op_str
        if ins.mnemonic.startswith('j') or ins.mnemonic == 'call':
            op = 'L'
        if jizz:
            op = re.sub(r'(\[e(?:bp|si) \+ (?:[a-z0-9]+\*?\d? \+ )?)0x([0-9a-f]+)', jmap, op)
        out.append(('%-6s %s' % (ins.mnemonic, op)).strip())
    return out


def main():
    want = sys.argv[1:]
    if want == ['list']:
        for n, ja, jb, ca, cb in PAIRS:
            print('%-12s JIZZ %#07x..%#07x (%3d bytes)  CLONE %#08x..%#08x (%3d bytes)' % (n, ja, jb, jb - ja, ca, cb,
                                                                                       cb - ca))
        return
    for n, ja, jb, ca, cb in PAIRS:
        if want and n not in want:
            continue
        j = norm(JIZZ, 0, ja, jb, True)
        c = norm(CLONE, CBASE, ca, cb, False)
        sm = difflib.SequenceMatcher(None, j, c, autojunk=False)
        print('=== %s  JIZZ %#x  CLONE %#x  (%d / %d instructions, similarity %.2f)' % (n, ja, ca, len(j), len(c),
                                                                                       sm.ratio()))
        for line in list(difflib.unified_diff(j, c, 'jizz', 'clone', n=1, lineterm=''))[2:]:
            print('   ' + line)


if __name__ == '__main__':
    main()
