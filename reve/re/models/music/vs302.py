"""Align LCA's MOD-Player V3.03 (REVE.LFL) with V3.02 (STARS.EXE) instruction by instruction, to find what changed.

Both are disassembled linearly over the matching ranges; addresses (STARS: image offsets 0x10000..0x80000, Reve:
linear addresses 0x200000..0x280000) are replaced by 'A' and the two instruction lists are aligned with difflib.
Prints the non-equal blocks with both addresses, and the address pairs of the equal blocks (the relocation map)
with --map.

usage: python3 vs302.py [--stars PATH/stars_img.bin] [--map] [--context 2]
(needs STARS.EXE unpacked by stars/re/unpack.py: stars/re/unpacked/stars_img.bin; the default path is the main
checkout's). addrmap.py and tables.py use the alignment to map data addresses and to compare the jump tables.
"""
import argparse
import difflib
import os
import re
import sys

import capstone

HERE = os.path.dirname(os.path.abspath(__file__))
RE = os.path.normpath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, RE)
import rep  # noqa: E402

STARS_IMG = os.path.join(RE, '..', '..', 'stars', 're', 'unpacked', 'stars_img.bin')
if not os.path.exists(STARS_IMG):              # (a worktree has no unpacked STARS: the main checkout's)
    STARS_IMG = '/home/dennis/src/dvdstelt/demoscene/stars/re/unpacked/stars_img.bin'
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
md.skipdata = True

# (STARS start, STARS end, Reve start, Reve end, what)
RANGES = [
    (0x19dd0, 0x1a1b0, 0x26e16c, 0x26e518, 'conversion, standard MOD (STARS: unused 0x19dd0; Reve: used)'),
    (0x1a1b0, 0x1ac04, 0x26e518, 0x26ef34, 'conversion 16CH-style, upload, start/stop, IRQ entry'),
    (0x1ac44, 0x1aef4, 0x26ef74, 0x26f224, 'tick effects (after the table 0x1ac04 / 0x26ef34)'),
    (0x1af34, 0x1b298, 0x26f264, 0x26f5cc, 'tick E effects, retrigger, row tick (after table 0x1aef4 / 0x26f224)'),
    (0x1b2d8, 0x1b45c, 0x26f60c, 0x26f790, 'row effects (after table 0x1b298 / 0x26f5cc)'),
    (0x1b49c, 0x1b976, 0x26f7d0, 0x26fd7c, 'row E effects, note, volume, frequency (after table 0x1b45c / 0x26f790)'),
    (0x1ba6c, 0x1c684, 0x25f698, 0x2601dc, 'detection'),
    (0x1c684, 0x1c7f8, 0x2601dc, 0x260340, 'entry points'),
]


def listing(img, base, a, b, lo, hi):
    out = []
    for ins in md.disasm(img[a - base:b - base], a):
        txt = ins.mnemonic + ' ' + ins.op_str
        norm = re.sub(r'0x([0-9a-f]+)', lambda m: 'A' if lo <= int(m.group(1), 16) < hi else m.group(0), txt)
        out.append((ins.address, txt, norm))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--stars', default=STARS_IMG)
    ap.add_argument('--context', type=int, default=1)
    ap.add_argument('--map', action='store_true')
    a = ap.parse_args()
    simg = open(a.stars, 'rb').read()
    for s0, s1, r0, r1, what in RANGES:
        print('=' * 100)
        print('%s: STARS %#x..%#x  Reve %#x..%#x' % (what, s0, s1, r0, r1))
        S = listing(simg, 0, s0, s1, 0x10000, 0x80000)
        R = listing(rep.IMG, rep.BASE, r0, r1, 0x200000, 0x280000)
        sm = difflib.SequenceMatcher(None, [x[2] for x in S], [x[2] for x in R], autojunk=False)
        for tag, i1, i2, j1, j2 in sm.get_opcodes():
            if tag == 'equal':
                if a.map:
                    print('  = STARS %#x..%#x  Reve %#x..%#x (%d)' % (S[i1][0], S[i2 - 1][0], R[j1][0], R[j2 - 1][0],
                                                                    i2 - i1))
                continue
            print('--- %s' % tag)
            for k in range(max(0, i1 - a.context), i1):
                print('    %06x %-40s | %06x %s' % (S[k][0], S[k][1], R[j1 - i1 + k][0] if j1 - i1 + k >= 0 else 0,
                                                     R[j1 - i1 + k][1] if j1 - i1 + k >= 0 else ''))
            n = max(i2 - i1, j2 - j1)
            for k in range(n):
                sl = '%06x %-40s' % (S[i1 + k][0], S[i1 + k][1]) if i1 + k < i2 else ' ' * 47
                rl = '%06x %s' % (R[j1 + k][0], R[j1 + k][1]) if j1 + k < j2 else ''
                print('  ! %s | %s' % (sl, rl))


def addr_pairs(simg):
    """(STARS address, Reve address) of every address operand in the aligned (equal) instructions."""
    pairs = {}
    for s0, s1, r0, r1, what in RANGES:
        S = listing(simg, 0, s0, s1, 0x10000, 0x80000)
        R = listing(rep.IMG, rep.BASE, r0, r1, 0x200000, 0x280000)
        sm = difflib.SequenceMatcher(None, [x[2] for x in S], [x[2] for x in R], autojunk=False)
        for tag, i1, i2, j1, j2 in sm.get_opcodes():
            if tag != 'equal':
                continue
            for k in range(i2 - i1):
                sa = [int(m, 16) for m in re.findall(r'0x([0-9a-f]+)', S[i1 + k][1]) if 0x10000 <= int(m, 16) < 0x80000]
                ra = [int(m, 16) for m in re.findall(r'0x([0-9a-f]+)', R[j1 + k][1])
                      if 0x200000 <= int(m, 16) < 0x280000]
                for x, y in zip(sa, ra):
                    pairs.setdefault(x, set()).add((y, S[i1 + k][0], R[j1 + k][0], S[i1 + k][1].split()[0]))
    return pairs


if __name__ == '__main__':
    main()
