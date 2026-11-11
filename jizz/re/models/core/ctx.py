"""ctx.py TARGET [N]: every call site of TARGET with the N (default 12) instructions before it, decoded by recursive
descent from all call targets so the listing is aligned."""
import sys, pickle, os
sys.path.insert(0, '/home/dennis/src/dvdstelt/demoscene/jizz/re')
import rep

CACHE = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'insns.pickle')

def all_insns():
    if os.path.exists(CACHE):
        return pickle.load(open(CACHE, 'rb'))
    seen = {}
    starts = list(rep.call_targets().keys()) + [0x186bd]
    todo = list(starts)
    done = set()
    while todo:
        a = todo.pop()
        if a in done: continue
        done.add(a)
        while 0 <= a < rep.PROGRAM and a not in seen:
            ins = next(rep.md.disasm(rep.SEG[a:a + 15], a), None)
            if ins is None: break
            seen[a] = (ins.size, ins.mnemonic, ins.op_str)
            m = ins.mnemonic
            if m.startswith('j') or m.startswith('loop') or m == 'call':
                try: todo.append(int(ins.op_str, 16))
                except ValueError: pass
                if m == 'jmp': break
            if m in ('ret', 'retf', 'iretd', 'iret'): break
            a += ins.size
    pickle.dump(seen, open(CACHE, 'wb'))
    return seen

if __name__ == '__main__':
    ins = all_insns()
    addrs = sorted(ins)
    import bisect
    tgt = int(sys.argv[1], 16)
    n = int(sys.argv[2]) if len(sys.argv) > 2 else 12
    for site in rep.call_targets().get(tgt, []):
        i = bisect.bisect_left(addrs, site)
        if i >= len(addrs) or addrs[i] != site:
            print(f'-- {site:06x}: not decoded'); continue
        print(f'-- call at {site:06x}')
        for a in addrs[max(0, i - n):i + 1]:
            s, m, o = ins[a]
            print(f'   {a:06x}: {m} {o}')
