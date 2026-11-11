"""xr.py ADDR...: for each address, the instructions that contain it as a 32-bit immediate or displacement, with
the nearest preceding call target (a guess of the enclosing routine)."""
import sys, bisect
sys.path.insert(0, '/home/dennis/src/dvdstelt/demoscene/jizz/re')
import rep
targets = sorted(rep.call_targets().keys())
def owner(a):
    i = bisect.bisect_right(targets, a) - 1
    return targets[i] if i >= 0 else 0
def find(site, v):
    for back in range(1, 12):
        st = site - back
        ins = next(rep.md.disasm(rep.SEG[st:st + 15], st), None)
        if ins and st + ins.size >= site + 4 and (f'{v:#x}' in ins.op_str):
            return ins
    return None
if __name__ == '__main__':
    for arg in sys.argv[1:]:
        v = int(arg, 16)
        print(f'== {v:#x}')
        for site in rep.xref(v):
            ins = find(site, v)
            if ins: print(f'  {ins.address:06x} (in {owner(ins.address):06x}): {ins.mnemonic} {ins.op_str}')
            else: print(f'  {site:06x}: (data?)')
