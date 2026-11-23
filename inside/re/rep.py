"""Disassembly helper for the unpacked INSIDE.EXE program (flat 32-bit code under PMODE/W).

Addresses are linear addresses of the running program: unpack.py loads object 1 (code, 0x1e8be bytes) at
0x400000 and object 2 (data 0x3c45 bytes, then BSS and stack up to 0x4ba6a0) at 0x420000, as run_inside.py does, so
address A is at offset A - 0x400000 of unpacked/inside_img.bin.

usage (inside the toolbox, capstone needed):
  python3 rep.py func 0x4010de       recursive-descent listing of one function (follows jumps, stops at ret)
  python3 rep.py dis 0x4010de 60     linear listing
  python3 rep.py calls 0x401000      call sites of a function
  python3 rep.py xref 0x41a000       code/data that contains this 32-bit immediate/displacement/pointer
  python3 rep.py funcs               all call targets with caller counts
  python3 rep.py str [minlen]        printable strings with addresses
  python3 rep.py ints                int 10h/16h/21h/31h/33h/386h sites with the AH/AX value loaded before
  python3 rep.py ports               in/out instructions with the port (immediate or dx loaded before)
In Python: import rep; rep.rd32(a), rep.rd16, rep.rdf (float), rep.rdd (double), rep.cstr(a), rep.dis, rep.func.
"""
import os
import re
import struct
import sys

import capstone

HERE = os.path.dirname(os.path.abspath(__file__))
BASE = 0x400000
IMG = open(os.path.join(HERE, 'unpacked', 'inside_img.bin'), 'rb').read()
PROGRAM = 0x423c45              # end of the initialised data (object 2's data)
BSS_END = BASE + len(IMG)        # end of object 2 (BSS and stack; the stack top is at 0x4ba6a0)
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)


def rd(a, n):
    """n bytes at linear address a (zero in the BSS)."""
    o = a - BASE
    if 0 <= o and o + n <= len(IMG):
        return IMG[o:o + n]
    return bytes(IMG[o:len(IMG)] if 0 <= o < len(IMG) else b'') + bytes(n)


def rd32(a): return struct.unpack('<I', rd(a, 4))[0]
def rd16(a): return struct.unpack('<H', rd(a, 2))[0]
def rdf(a): return struct.unpack('<f', rd(a, 4))[0]
def rdd(a): return struct.unpack('<d', rd(a, 8))[0]


def cstr(a, n=60):
    s = rd(a, n)
    e = 0
    while e < len(s) and 32 <= s[e] < 127: e += 1
    return s[:e].decode('latin1')


def annotate(ins):
    txt = f'{ins.address:06x}: {ins.mnemonic:6s} {ins.op_str}'
    for m in re.finditer(r'0x([0-9a-f]{6,7})\b', ins.op_str):
        v = int(m.group(1), 16)
        if BASE <= v < PROGRAM:
            s = cstr(v)
            if len(s) >= 4: txt += f'   ; "{s}"'
            if ins.mnemonic.startswith('f') and 'dword ptr [0x' in ins.op_str: txt += f'   ; ={rdf(v):.6g}'
            if ins.mnemonic.startswith('f') and 'qword ptr [0x' in ins.op_str: txt += f'   ; ={rdd(v):.9g}'
        elif PROGRAM <= v < BSS_END:
            txt += '   ; bss'
    return txt


def dis(start, n=60):
    out = []
    for ins in md.disasm(rd(start, n * 15), start):
        out.append(annotate(ins))
        if len(out) >= n: break
    return '\n'.join(out)


def func(start, limit=20000):
    seen, todo, out = set(), [start], {}
    while todo and len(out) < limit:
        a = todo.pop()
        while a not in seen and BASE <= a < PROGRAM:
            seen.add(a)
            ins = next(md.disasm(rd(a, 15), a), None)
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
    for i in range(len(IMG) - 5):
        if IMG[i] == 0xE8:
            dst = BASE + i + 5 + struct.unpack_from('<i', IMG, i + 1)[0]
            if BASE <= dst < PROGRAM: t.setdefault(dst, []).append(BASE + i)
    return t


def xref(v):
    pat = struct.pack('<I', v)
    return [BASE + m.start() for m in re.finditer(re.escape(pat), IMG)]


def strings(minlen=5):
    for m in re.finditer(rb'[\x20-\x7e]{%d,}' % minlen, IMG):
        yield BASE + m.start(), m.group().decode('latin1')


SERVICES = (0x10, 0x16, 0x21, 0x2f, 0x31, 0x33)


def _before(i, n=8):
    """Instructions decoded backwards so that they end exactly at image offset i."""
    for back in range(40, 0, -1):
        seq = list(md.disasm(IMG[i - back:i], BASE + i - back))
        if seq and seq[-1].address + seq[-1].size == BASE + i and sum(s.size for s in seq) == back:
            return seq[-n:]
    return []


def ints():
    """int instructions for the usual services (byte scan, so data can give false hits), with the value moved
    into ax/ah/eax just before (looked back over up to 8 instructions)."""
    out = []
    for i in range(len(IMG) - 2):
        if IMG[i] != 0xcd or IMG[i + 1] not in SERVICES: continue
        ctx = ''
        for s in _before(i):
            if s.mnemonic == 'mov' and re.match(r'(e?ax|ah|al)\b', s.op_str):
                ctx = s.op_str
        out.append(f'{BASE + i:06x}: int {IMG[i + 1]:#x} {ctx}')
    return out


def ports():
    out = []
    for i in range(len(IMG)):
        b = IMG[i]
        if b in (0xe4, 0xe5, 0xe6, 0xe7) or (b in (0xec, 0xed, 0xee, 0xef)):
            pre = IMG[i - 1] if i else 0
            ins = next(md.disasm(IMG[i - (pre == 0x66):i + 2], BASE + i - (pre == 0x66)), None)
            if ins is None: continue
            ctx = ''
            if b >= 0xec:
                for s in _before(i - (pre == 0x66)):
                    if re.match(r'(e?dx|dl)\b', s.op_str) and s.mnemonic in ('mov', 'add', 'sub', 'inc', 'dec',
                                                                             'or', 'lea'):
                        ctx += s.mnemonic + ' ' + s.op_str + '; '
            out.append(f'{ins.address:06x}: {ins.mnemonic} {ins.op_str:10s} {ctx}')
    return out


if __name__ == '__main__':
    cmd = sys.argv[1]
    if cmd == 'func': print(func(int(sys.argv[2], 16)))
    elif cmd == 'dis': print(dis(int(sys.argv[2], 16), int(sys.argv[3]) if len(sys.argv) > 3 else 60))
    elif cmd == 'calls': print(' '.join(hex(x) for x in call_targets().get(int(sys.argv[2], 16), [])))
    elif cmd == 'xref': print(' '.join(hex(x) for x in xref(int(sys.argv[2], 16))))
    elif cmd == 'str':
        for a, s in strings(int(sys.argv[2]) if len(sys.argv) > 2 else 5): print(f'{a:06x}: {s}')
    elif cmd == 'ints': print('\n'.join(ints()))
    elif cmd == 'ports': print('\n'.join(ports()))
    elif cmd == 'funcs':
        t = call_targets()
        for dst in sorted(t):
            if len(t[dst]) >= 2 or rd(dst, 1)[0] in (0x55, 0x60, 0x53, 0x56, 0x57, 0x51, 0x52):
                print(f'{dst:06x} callers={len(t[dst])}')
