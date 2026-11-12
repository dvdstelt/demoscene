"""For every overlay load (0x4a9f3 call site), find where the returned pointer is stored and list the instructions
that read that variable (to see how each file is used)."""
import bisect, os, re, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
import rep
import loads

A = loads.addrs


def ins_at(x):
    i = bisect.bisect_right(A, x) - 1
    if i < 0: return None
    s = loads.INS[A[i]]
    return s if A[i] <= x < A[i] + s.size else None


def after(site, n=4):
    i = bisect.bisect_left(A, site)
    return [loads.INS[A[j]] for j in range(i + 1, min(i + 1 + n, len(A)))]


def users(var):
    out = []
    for x in rep.xref(var):
        s = ins_at(x)
        if s: out.append(s)
    return out


if __name__ == '__main__':
    only = set(int(a) for a in sys.argv[1:])
    calls = rep.call_targets()
    for site in calls[0x4a9f3]:
        n = loads.eax_before(site)
        if only and n not in only: continue
        print(f'=== file {n} loaded at {site:#x} (in {loads.owner(site):#x})')
        for s in after(site):
            print(f'    {s.address:06x}: {s.mnemonic} {s.op_str}')
            m = re.match(r'dword ptr \[(0x[0-9a-f]+)\], eax', s.op_str)
            if s.mnemonic == 'mov' and m:
                v = int(m.group(1), 16)
                for u in users(v):
                    print(f'        use {u.address:06x} (in {loads.owner(u.address):#x}): {u.mnemonic} {u.op_str}')
                break
