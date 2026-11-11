"""Disassembly helper for the unpacked PAPER.EXE (Tran's PMODE, 32-bit code segment at image offset 0xd00).

Addresses are code32 offsets (what the 32-bit code uses). The image file is paper_img.bin
(load segment 0x1000 onwards, produced by unpklite.py).

usage (inside the toolbox, capstone needed):
  python3 rep.py func 0x1049        recursive-descent listing of one function
  python3 rep.py dis 0x1049 60      linear listing
  python3 rep.py calls 0x1049       call sites of a function
  python3 rep.py xref 0x45ee        code that contains this 32-bit immediate/displacement
  python3 rep.py funcs              all call targets with caller counts
"""
import os
import re
import struct
import sys

import capstone

HERE = os.path.dirname(os.path.abspath(__file__))
IMG = open(os.path.join(HERE, 'paper_img.bin'), 'rb').read()
C32 = 0xd00
SEG = IMG[C32:]
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)


def rd32(a): return struct.unpack_from('<I', SEG, a)[0]
def rdf(a): return struct.unpack_from('<f', SEG, a)[0]
def rdd(a): return struct.unpack_from('<d', SEG, a)[0]


def cstr(a, n=60):
    e = a
    while e < len(SEG) and e - a < n and 32 <= SEG[e] < 127: e += 1
    return SEG[a:e].decode('latin1')


def annotate(ins):
    txt = f'{ins.address:06x}: {ins.mnemonic:6s} {ins.op_str}'
    for m in re.finditer(r'0x([0-9a-f]{3,6})\b', ins.op_str):
        v = int(m.group(1), 16)
        if v < len(SEG):
            s = cstr(v)
            if len(s) >= 4: txt += f'   ; "{s}"'
            if ins.mnemonic.startswith('f') and 'dword ptr [0x' in ins.op_str: txt += f'   ; ={rdf(v):.6g}'
            if ins.mnemonic.startswith('f') and 'qword ptr [0x' in ins.op_str: txt += f'   ; ={rdd(v):.9g}'
    return txt


def dis(start, n=60):
    out = []
    for ins in md.disasm(SEG[start:start + n * 15], start):
        out.append(annotate(ins))
        if len(out) >= n: break
    return '\n'.join(out)


def func(start, limit=20000):
    seen, todo, out = set(), [start], {}
    while todo and len(out) < limit:
        a = todo.pop()
        while a not in seen and 0 <= a < len(SEG):
            seen.add(a)
            ins = next(md.disasm(SEG[a:a + 15], a), None)
            if ins is None: break
            out[a] = ins
            m = ins.mnemonic
            if m.startswith('j') or m.startswith('loop'):
                try: todo.append(int(ins.op_str, 16))
                except ValueError: pass
                if m == 'jmp': break
            if m in ('ret', 'retf', 'iretd', 'iret'): break
            a += ins.size
    return '\n'.join(annotate(out[a]) for a in sorted(out))


def call_targets():
    t = {}
    for i in range(len(SEG) - 5):
        if SEG[i] == 0xE8:
            dst = i + 5 + struct.unpack_from('<i', SEG, i + 1)[0]
            if 0 <= dst < len(SEG): t.setdefault(dst, []).append(i)
    return t


def xref(v):
    pat = struct.pack('<I', v)
    return [m.start() for m in re.finditer(re.escape(pat), SEG)]


if __name__ == '__main__':
    cmd = sys.argv[1]
    if cmd == 'func': print(func(int(sys.argv[2], 16)))
    elif cmd == 'dis': print(dis(int(sys.argv[2], 16), int(sys.argv[3]) if len(sys.argv) > 3 else 60))
    elif cmd == 'calls': print(' '.join(hex(x) for x in call_targets().get(int(sys.argv[2], 16), [])))
    elif cmd == 'xref': print(' '.join(hex(x) for x in xref(int(sys.argv[2], 16))))
    elif cmd == 'funcs':
        t = call_targets()
        # keep targets that are called from at least 2 places or look like a prologue
        for dst in sorted(t):
            if len(t[dst]) >= 2 or SEG[dst] in (0x55, 0x60, 0x53, 0x56, 0x57):
                print(f'{dst:06x} callers={len(t[dst])}')
