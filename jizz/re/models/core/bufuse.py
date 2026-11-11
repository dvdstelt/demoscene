"""bufuse.py: for each buffer pointer variable set in main, the routines that reference it."""
import sys, bisect
sys.path.insert(0, '/home/dennis/src/dvdstelt/demoscene/jizz/re/models/core')
sys.path.insert(0, '/home/dennis/src/dvdstelt/demoscene/jizz/re')
import rep
from xr import find, owner
from layout import PTRS
for p, size in PTRS:
    users = []
    for site in rep.xref(p):
        ins = find(site, p)
        if ins and ins.address not in range(0x18732, 0x188f3):
            users.append(f'{ins.address:x}({owner(ins.address):x})')
    print(f'[{p:#x}] {size:#x}: ' + ' '.join(users))
