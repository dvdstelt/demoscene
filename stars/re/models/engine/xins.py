"""Print the instruction that references an address (rep.py xref gives the operand's position)."""
import sys, os
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
import rep

def ins_at(pos, target):
    best = None
    for back in range(11, 0, -1):
        st = pos - back
        for ins in rep.md.disasm(rep.SEG[st:st + 16], st):
            if ins.address <= pos < ins.address + ins.size and f'{target:x}' in ins.op_str:
                score = ('[0x' + f'{target:x}' in ins.op_str) * 100 + ins.size
                if best is None or score > best[0]: best = (score, ins)
            break
    return best[1] if best else None

if __name__ == '__main__':
    for t in sys.argv[1:]:
        tv = int(t, 16)
        out = os.popen(f'python3 {os.path.join(os.path.dirname(rep.__file__), "rep.py")} xref {t}').read().split()
        print(f'== {t}')
        for p in out:
            p = int(p, 16)
            i = ins_at(p, tv)
            print(f'  {i.address:06x}: {i.mnemonic} {i.op_str}' if i else f'  {p:06x}: ?')
