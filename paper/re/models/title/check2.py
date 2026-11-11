import pickle, struct, sys
import unicorn
import numpy as np
sys.path.insert(0, '.')
import model
snap = pickle.load(open(sys.argv[1], 'rb')); mem = snap['mem']
r32 = lambda a: struct.unpack_from('<I', mem, a)[0]
g, _ = model.font()
sp = model.sprites(g)
bad = 0
for c in range(26):
    p = r32(r32(0x1edcd) + 4*c)
    m = np.frombuffer(mem, np.uint8, 0xc80, p).reshape(40, 80)
    if not np.array_equal(m, sp[c]):
        bad += 1; print('sprite', c, 'diff', (m != sp[c]).sum(), np.argwhere(m != sp[c])[:5])
print('sprites bad', bad)
rec = r32(0x1edcd) + 0x400
bb = r32(0x1edf9)
for i in range(25):
    ch = mem[rec + 24*i]; pos = struct.unpack_from('<I', mem, rec+24*i+4)[0] - bb
    offs = struct.unpack_from('<4i', mem, rec+24*i+8)
    print(i, repr(chr(ch)), (pos % 320, pos // 320), [(o % 320 if o % 320 < 160 else o % 320 - 320, (o + 160) // 320) for o in offs])
