"""layout.py: the buffer pointers main sets at 0x18732..0x188ed, checked against the t430 snapshot."""
import sys
sys.path.insert(0, '/home/dennis/src/dvdstelt/demoscene/jizz/re/models/core')
from snap import load, rd32
# (pointer variable, size added after it) in the order of main's code
PTRS = [(0x652a, 0x20000), (0x6546, 0x100000), (0x654a, 0x10000)] + \
       [(0x654e + 4 * i, 0x30000) for i in range(16)] + [(0x658e, 0x20000),
       (0x10fd2, 0x13000), (0x10fd6, 0x82000), (0x1130f, 0x2000), (0x11313, 0x2000), (0x11317, 0x2000)] + \
       [(0x12425 + 4 * i, 0x30000) for i in range(5)] + [(0x145dc, 0x15ede0), (0x15dce, 0x10000),
       (0x16f4b, 0xc8000), (0x179f2, 0x200), (0x18005, 0x12e0), (0x181f0, 0x500)]
s = load(430)
base = rd32(s, 0x204e)
off = 0
for p, size in PTRS:
    v = rd32(s, p)
    print(f'[{p:#07x}] = base+{off:#08x} (size {size:#08x})  snapshot {v:#x}  {"ok" if v == base + off else "CHANGED (+%#x)" % (v - base - off)}')
    off += size
print(f'total {off:#x} (allocated 0x8137c0)')
