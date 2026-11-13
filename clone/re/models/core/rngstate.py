"""The shared random generator at 0x420491 (inlined at 31 sites in 0x4051d5, 0x40590c, 0x40600f, 0x40628f):
seed = seed * 0x41c64e6d + 0x3039 (mod 2^32), value = (seed >> 10) & 0x7fff; seed 1 in the image.
Prints how many numbers had been drawn at each snapshot (steps from seed 1).

usage (toolbox, from clone/re): python3 models/core/rngstate.py
"""
import glob
import os
import pickle
import struct

import unicorn  # noqa: F401

RE = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
snaps = {}
for path in glob.glob(os.path.join(RE, 'snaps', 't*.snap')):
    s = pickle.load(open(path, 'rb'))
    snaps[int(os.path.basename(path)[1:-5])] = struct.unpack('<I', s['mem'][0x420491:0x420495])[0]
want = set(snaps.values())
seed, n, found = 1, 0, {}
while len(found) < len(want) and n < 50_000_000:
    if seed in want and seed not in found: found[seed] = n
    seed = (seed * 0x41c64e6d + 0x3039) & 0xffffffff
    n += 1
for t, s in sorted(snaps.items()):
    print(f't{t:<6d} seed {s:#010x}  numbers drawn since the start: {found.get(s, "not found")}')
