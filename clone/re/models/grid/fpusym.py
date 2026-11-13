"""Symbolic x87 tracer: follows a straight path of instructions and prints what each store / fistp / compare
receives, as an expression of the loaded values. Used to read the long FPU sequences of parts 2, 4 and 5.

usage (from clone/re): python3 models/grid/fpusym.py START:END[,START:END...] [name=addr ...]
  The path is the concatenation of the address ranges (END exclusive); conditional jumps are not followed (give
  the ranges of the path you want), unconditional jumps are ignored as well (the ranges decide).
  name=addr gives a name to a memory operand, e.g. T=0xea6be4 or 'X=[ebp - 0x18]'.
"""
import os
import re
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
import rep  # noqa: E402

NAMES = {}
SHORT = True


def memkey(op):
    m = re.search(r'\[(.*)\]', op)
    k = m.group(1).replace(' ', '')
    return k


def memname(op):
    k = memkey(op)
    if k in NAMES: return NAMES[k]
    return '[' + k + ']'


def const(op):
    """value of a constant in the image, or None."""
    k = memkey(op)
    try:
        a = int(k, 16)
    except ValueError:
        return None
    if not (rep.BASE <= a < rep.PROGRAM): return None
    if 'qword' in op: return repr(rep.rdd(a))
    if 'dword' in op: return repr(rep.rdf(a))
    return None


def P(e):
    return e if re.fullmatch(r'[\w.\[\]+\-]*', e) and not e.startswith('-') or e.startswith('(') and balanced(e) \
        else '(' + e + ')'


def balanced(e):
    d = 0
    for i, c in enumerate(e):
        d += c == '('
        d -= c == ')'
        if d == 0 and i < len(e) - 1: return False
    return True


def trace(ranges):
    st = []
    mem = {}
    ver = {}
    regs = {}

    def rd(op):
        k = memkey(op)
        if k in mem: return mem[k]
        if k in NAMES: return NAMES[k]
        c = const(op)
        if c is not None: return c
        return memname(op)

    def push(e): st.insert(0, e)

    def pop(): return st.pop(0)

    for a0, a1 in ranges:
        a = a0
        while a < a1:
            ins = next(rep.md.disasm(rep.rd(a, 15), a))
            a += ins.size
            m, op, b = ins.mnemonic, ins.op_str, bytes(ins.bytes)
            out = None
            if m == 'mov' and 'ptr' in op:
                d, s = [x.strip() for x in op.split(',', 1)]
                if 'ptr' in s and d in ('eax', 'ebx', 'ecx', 'edx', 'esi', 'edi'):
                    regs[d] = rd(s)
                elif 'ptr' in d and s in regs and 'dword' in d:
                    mem[memkey(d)] = regs[s]
                    out = f'{memname(d)} = {regs[s]}'
                elif 'ptr' in d and s in ('eax', 'ebx', 'ecx', 'edx', 'esi', 'edi') and 'dword' in d:
                    mem[memkey(d)] = f'{s}@{ins.address:x}'
                    out = f'{memname(d)} = {s} (integer, at {ins.address:x})'
                elif 'ptr' in d and s.startswith('0x') and 'dword' in d:
                    v = struct.unpack('<f', struct.pack('<I', int(s, 16)))[0]
                    mem[memkey(d)] = repr(v)
                    out = f'{memname(d)} = {v!r} (int {s})'
            if m in ('mov', 'imul', 'lea', 'add', 'sub', 'shl', 'xor', 'inc') and not m.startswith('f'):
                d = op.split(',')[0].strip()
                if d in regs and not (m == 'mov' and 'ptr' in op): regs.pop(d, None)
            if not m.startswith('f'):
                if m.startswith('j') or m.startswith('call') or m in ('sahf', 'fnstsw'):
                    print(f'{ins.address:06x}: {m} {op}')
                continue
            regform = 'ptr' not in op
            i = b[-1] & 7 if regform else None
            if m == 'fld':
                push(st[i] if regform else rd(op))
            elif m == 'fild':
                push('int' + P(rd(op)))
            elif m == 'fld1': push('1')
            elif m == 'fldz': push('0')
            elif m == 'fldl2e': push('log2(e)')
            elif m == 'fldln2': push('ln2')
            elif m in ('fst', 'fstp'):
                if regform:
                    st[i] = st[0]
                else:
                    k = memkey(op)
                    ver[k] = ver.get(k, 0) + 1
                    nm = (NAMES.get(k) or re.sub(r'\W', '', k.replace('ebp-0x', 'm').replace('ebp+0x', 'p'))) \
                        + "'" * ver[k]
                    mem[k] = nm if SHORT else st[0]
                    out = f'{nm} = {st[0]}   ({op.split()[0]})'
                if m == 'fstp': pop()
            elif m == 'fistp':
                mem[memkey(op)] = 'trunc' + P(st[0])
                out = f'{memname(op)} = trunc({st[0]})'
                pop()
            elif m == 'fxch':
                st[0], st[i] = st[i], st[0]
            elif m == 'fchs': st[0] = '-' + P(st[0])
            elif m in ('fsin', 'fcos', 'fsqrt'): st[0] = m[1:] + '(' + st[0] + ')'
            elif m == 'fpatan':
                y = st[1]
                x = st[0]
                pop()
                st[0] = f'atan2({y}, {x})'
            elif m in ('fcomp', 'fcom', 'fcompp'):
                rhs = st[1] if regform else rd(op)
                out = f'compare {st[0]} with {rhs}'
                pop()
            elif m in ('fadd', 'fmul', 'fsub', 'fsubr', 'fdiv', 'fdivr') and not regform:
                v = rd(op)
                sym = {'fadd': '+', 'fmul': '*', 'fsub': '-', 'fsubr': '-r', 'fdiv': '/', 'fdivr': '/r'}[m]
                if sym.endswith('r'): st[0] = f'{P(v)} {sym[0]} {P(st[0])}'
                else: st[0] = f'{P(st[0])} {sym} {P(v)}'
            elif regform and b[0] in (0xd8, 0xdc, 0xde):
                r = b[1] & 0xf8
                kind = {0xc0: 'add', 0xc8: 'mul', 0xe0: 'sub', 0xe8: 'subr', 0xf0: 'div', 0xf8: 'divr'}[r]
                if b[0] == 0xd8:               # st0 = st0 op st(i)
                    dst, x, y = 0, st[0], st[i]
                else:                          # st(i) = st(i) op st0, encodings swapped for sub/div
                    kind = {'sub': 'subr', 'subr': 'sub', 'div': 'divr', 'divr': 'div'}.get(kind, kind)
                    dst, x, y = i, st[i], st[0]
                s = {'add': '+', 'mul': '*', 'sub': '-', 'subr': '-', 'div': '/', 'divr': '/'}[kind]
                st[dst] = f'{P(y)} {s} {P(x)}' if kind.endswith('r') else f'{P(x)} {s} {P(y)}'
                if b[0] == 0xde: pop()
            elif m in ('fnstsw', 'fwait', 'fninit'):
                pass
            else:
                out = f'?? unhandled {m} {op}'
            if out: print(f'{ins.address:06x}: {out}')
    print('stack at the end:', st)


if __name__ == '__main__':
    rs = []
    for part in sys.argv[1].split(','):
        x, y = part.split(':')
        rs.append((int(x, 16), int(y, 16)))
    for kv in sys.argv[2:]:
        n, v = kv.split('=', 1)
        if v.startswith('['): NAMES[v[1:-1].replace(' ', '')] = n
        else: NAMES[v] = n
    trace(rs)
