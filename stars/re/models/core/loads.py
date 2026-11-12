"""List every call of the overlay loader 0x4a9f3 (and 0x17e50, show picture) with the file number loaded
into EAX just before. Uses a recursive-descent decode from every call target so that instruction boundaries are
right; the value is the last `mov eax/ax/al, imm` before the call in address order within 40 bytes."""
import os, sys, re
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
import rep

calls = rep.call_targets()
INS = {}


def decode(start):
    todo = [start]
    while todo:
        a = todo.pop()
        while a not in INS and 0 <= a < rep.PROGRAM:
            ins = next(rep.md.disasm(rep.SEG[a:a + 15], a), None)
            if ins is None: break
            INS[a] = ins
            m = ins.mnemonic
            if m.startswith('j') or m.startswith('loop') or m == 'call':
                try: todo.append(int(ins.op_str, 16))
                except ValueError: pass
                if m == 'jmp': break
            if m in ('ret', 'retf', 'iretd', 'iret'): break
            a += ins.size


decode(0x178c3)
for t in calls: decode(t)
addrs = sorted(INS)
import bisect


def eax_before(site):
    i = bisect.bisect_left(addrs, site)
    for j in range(i - 1, max(i - 12, -1), -1):
        s = INS[addrs[j]]
        if site - s.address > 60: break
        m = re.match(r'(eax|ax|al), (0x[0-9a-f]+|\d+)$', s.op_str)
        if s.mnemonic == 'mov' and m: return int(m.group(2), 0)
        if re.match(r'(eax|ax|al|ah)\b', s.op_str) or s.mnemonic in ('pop', 'call', 'xchg', 'lodsb', 'lodsd'):
            return s.mnemonic + ' ' + s.op_str
    return '?'


def owner(a):
    best = None
    for s in sorted(calls):
        if s <= a: best = s
        else: break
    return best


if __name__ == '__main__':
    for tgt, name in ((0x4a9f3, 'load'), (0x17e50, 'show')):
        for site in calls.get(tgt, []):
            v = eax_before(site)
            vs = f'{v} ({v:#x})' if isinstance(v, int) else v
            print(f'{name} site {site:#07x} in {owner(site):#07x}: eax = {vs}')
