"""Disassemble code in a snapshot's memory (the loaded GUS driver SDIGUS.DSD lives at 0x1152xxx..0x1156xxx).
python3 snapdis.py ../../snaps/f38.snap dis 0x1153576 80
python3 snapdis.py ../../snaps/f38.snap func 0x1153576
python3 snapdis.py ../../snaps/f38.snap hex 0x1153000 256"""
import pickle, re, sys
import capstone
import unicorn  # noqa: F401

md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)


def load(path):
    return pickle.load(open(path, 'rb'))['mem']


def dis(mem, a, n):
    out = []
    for ins in md.disasm(bytes(mem[a:a + n * 15]), a):
        out.append('%07x: %-6s %s' % (ins.address, ins.mnemonic, ins.op_str))
        if len(out) >= n:
            break
    return out


def func(mem, start):
    seen, todo, out = set(), [start], {}
    while todo:
        a = todo.pop()
        while a not in seen:
            seen.add(a)
            ins = next(md.disasm(bytes(mem[a:a + 15]), a), None)
            if ins is None:
                break
            out[a] = '%07x: %-6s %s' % (ins.address, ins.mnemonic, ins.op_str)
            if ins.mnemonic.startswith('j') or ins.mnemonic in ('loop', 'jecxz', 'jcxz'):
                m = re.match(r'0x([0-9a-f]+)$', ins.op_str)
                if m:
                    todo.append(int(m.group(1), 16))
                if ins.mnemonic == 'jmp':
                    break
            if ins.mnemonic in ('ret', 'retf', 'iretd', 'iret'):
                break
            a += ins.size
    return [out[k] for k in sorted(out)]


if __name__ == '__main__':
    mem = load(sys.argv[1])
    cmd, a = sys.argv[2], int(sys.argv[3], 16)
    if cmd == 'dis':
        print('\n'.join(dis(mem, a, int(sys.argv[4], 0) if len(sys.argv) > 4 else 60)))
    elif cmd == 'func':
        print('\n'.join(func(mem, a)))
    elif cmd == 'hex':
        n = int(sys.argv[4], 0) if len(sys.argv) > 4 else 256
        for i in range(0, n, 16):
            b = mem[a + i:a + i + 16]
            print('%07x: %s  %s' % (a + i, b.hex(' '), ''.join(chr(c) if 32 <= c < 127 else '.' for c in b)))
