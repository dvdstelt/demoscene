"""Check the structure of the unrolled rotozoom code between 0x25a7dd and 0x25eae9 (patched by 0x25a1bc).

usage: python3 unrolled.py
Prints, for each of the four variants, its texel loads (register, texture placeholder), its dword stores, the
coordinate steps and where it jumps back to.
"""
import capstone

from common import rep

VARIANTS = ((0x25a7dd, 0x25b8a0, 'layer 1, even pixels'), (0x25b8a0, 0x25c963, 'layer 1, odd pixels'),
            (0x25c963, 0x25da26, 'layer 2, even pixels'), (0x25da26, 0x25eae9, 'layer 2, odd pixels'))


def main():
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    for a, b, what in VARIANTS:
        ins = list(md.disasm(rep.rd(a, b - a), a))
        loads = [i.op_str for i in ins if i.mnemonic == 'mov' and i.op_str[:3] in ('al,', 'ah,') and '[ebx' in i.op_str]
        stores = [i.op_str for i in ins if i.mnemonic == 'mov' and i.op_str.startswith('dword ptr [edi')]
        shifts = sorted({i.op_str for i in ins if i.mnemonic in ('shr', 'shld')})
        steps = sum(1 for i in ins if i.mnemonic == 'add' and i.op_str == 'ecx, esi')
        regs = sorted({s[:2] for s in loads})
        tex = sorted({s.split('[ebx ')[1] for s in loads})
        print('%#x..%#x %-22s %d texel loads into %s from %s, %d dword stores %s .. %s, %d steps, %s, then %s %s' % (
            a, b, what, len(loads), regs, tex, len(stores), stores[0], stores[-1], steps, shifts,
            ins[-1].mnemonic, ins[-1].op_str))


if __name__ == '__main__':
    main()
