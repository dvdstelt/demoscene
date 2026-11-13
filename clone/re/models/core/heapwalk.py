"""Walk the heap (0x400ff4 / 0x40100c) in a snapshot: block headers are size | 1 (used), the data follows.

usage (toolbox, from clone/re): python3 models/core/heapwalk.py [snaps/t11.snap]
"""
import os
import pickle
import struct
import sys

import unicorn  # noqa: F401

RE = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
s = pickle.load(open(sys.argv[1] if len(sys.argv) > 1 else os.path.join(RE, 'snaps', 't11.snap'), 'rb'))
mem = s['mem']
def d(a): return struct.unpack('<I', mem[a:a + 4])[0]
start, size = d(0x4207b0), d(0x4207b4)
print('heap start %#x size %d end %#x' % (start, size, start + size))
# pointers to look up the owner of each block
names = {}
for a in (0xe35108, 0xe3510c, 0xe35110, 0xe35124, 0xe35120, 0xe3511c, 0xe35118, 0xe350fc, 0xe350f8, 0xe350f4,
          0xe350f0, 0xf73930, 0xf7392c, 0xf613e8, 0xf613ec, 0xf613f0, 0xf73594, 0xf73580, 0xf73598, 0xf7391c,
          0xf73904, 0xf73918):
    names[d(a)] = '[%#x]' % a
a = start
n = 0
while a < start + size:
    h = d(a)
    used = h & 1
    sz = h & ~3
    data = a + 4
    who = names.get(data, '')
    if not who and used and sz == 12:
        who = 'picture header {pixels %#x, w %d, h %d}' % (d(data), d(data + 4), d(data + 8))
    print('%#9x  %9d (%#8x) %s %s' % (data, sz, sz, 'used' if used else 'FREE', who))
    a = data + sz
    n += 1
print(n, 'blocks')
