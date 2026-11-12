"""Disassembly helper for the unpacked STARS.EXE (PMODE 3.07 program; flat 32-bit code segment = image offset 0).

Addresses are offsets in the program image (unpacked/stars_img.bin from unpack.py). The 32-bit code and data
selectors have their base at the load address, so an image offset is also the program's near pointer ("code32
offset"). The 16-bit protected-mode kernel lives at 0x7a210.. (segment 0x7a21 relative to the load segment); use
`dis16` for it.

usage (inside the toolbox, capstone needed):
  python3 rep.py func 0x178c3       recursive-descent listing of one function
  python3 rep.py dis 0x178c3 60     linear listing
  python3 rep.py dis16 0x7a210 60   linear listing of 16-bit code (image offset)
  python3 rep.py calls 0x178c3      call sites of a function
  python3 rep.py xref 0x17620       code that contains this 32-bit immediate/displacement
  python3 rep.py funcs              all call targets with caller counts
  python3 rep.py str [minlen]       printable strings with offsets
  python3 rep.py ints               int 10h/16h/21h/2Fh/31h/33h sites with the AH/AX value loaded before

In Python (`import rep`): rep.SEG (the image), rep.rd8/rd16/rd32/rdf/rdd(addr), rep.cstr, rep.dis, rep.func.
"""
import os
import re
import struct
import sys

import capstone

HERE = os.path.dirname(os.path.abspath(__file__))
IMG = open(os.path.join(HERE, 'unpacked', 'stars_img.bin'), 'rb').read()
SEG = IMG
PROGRAM = 0x7c0ab              # end of the decompressed image (the rest up to 0x840b0 is stack / uninitialised)
CODE16 = 0x7a210               # the 16-bit kernel (PMODE 3.07, modified) up to the stack segment at 0x7c0b0
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
md16 = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_16)


def rd8(a): return SEG[a]
def rd32(a): return struct.unpack_from('<I', SEG, a)[0]
def rd16(a): return struct.unpack_from('<H', SEG, a)[0]
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
        if v < PROGRAM:
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


def dis16(start, n=60):
    out = []
    for ins in md16.disasm(SEG[start:start + n * 8], start):
        out.append(f'{ins.address:06x} ({ins.address - CODE16:04x}): {ins.mnemonic:6s} {ins.op_str}')
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
    for i in range(PROGRAM - 5):
        if SEG[i] == 0xE8:
            dst = i + 5 + struct.unpack_from('<i', SEG, i + 1)[0]
            if 0 <= dst < PROGRAM: t.setdefault(dst, []).append(i)
    return t


def xref(v):
    pat = struct.pack('<I', v)
    return [m.start() for m in re.finditer(re.escape(pat), SEG[:PROGRAM])]


def strings(minlen=5):
    for m in re.finditer(rb'[\x20-\x7e]{%d,}' % minlen, SEG[:PROGRAM]):
        yield m.start(), m.group().decode('latin1')


SERVICES = (0x10, 0x16, 0x21, 0x2f, 0x31, 0x33)


def ints():
    """int 10h/16h/21h/2Fh/31h/33h instructions (byte scan, so data can produce false hits), with the value moved
    into ax/ah/eax just before (looked back over up to 8 instructions)."""
    out = []
    for i in range(PROGRAM - 2):
        if SEG[i] != 0xcd or SEG[i + 1] not in SERVICES: continue
        ins = next(md.disasm(SEG[i:i + 2], i), None)
        if ins is None: continue
        # find a decoding that ends exactly at i
        ctx = ''
        for back in range(1, 40):
            seq = list(md.disasm(SEG[i - back:i + 2], i - back))
            if seq and seq[-1].address == i and sum(s.size for s in seq) == back + 2:
                for s in seq[-8:-1]:
                    if s.mnemonic == 'mov' and re.match(r'(e?ax|ah|al)\b', s.op_str):
                        ctx = s.op_str
                if len(seq) >= 6: break
        out.append(f'{i:06x}: int {ins.op_str:5s} {ctx}')
    return out


if __name__ == '__main__':
    cmd = sys.argv[1]
    if cmd == 'func': print(func(int(sys.argv[2], 16)))
    elif cmd == 'dis': print(dis(int(sys.argv[2], 16), int(sys.argv[3]) if len(sys.argv) > 3 else 60))
    elif cmd == 'dis16': print(dis16(int(sys.argv[2], 16), int(sys.argv[3]) if len(sys.argv) > 3 else 60))
    elif cmd == 'calls': print(' '.join(hex(x) for x in call_targets().get(int(sys.argv[2], 16), [])))
    elif cmd == 'xref': print(' '.join(hex(x) for x in xref(int(sys.argv[2], 16))))
    elif cmd == 'str':
        for a, s in strings(int(sys.argv[2]) if len(sys.argv) > 2 else 5): print(f'{a:06x}: {s}')
    elif cmd == 'ints': print('\n'.join(ints()))
    elif cmd == 'funcs':
        t = call_targets()
        for dst in sorted(t):
            if len(t[dst]) >= 2 or SEG[dst] in (0x55, 0x60, 0x53, 0x56, 0x57):
                print(f'{dst:06x} callers={len(t[dst])}')
