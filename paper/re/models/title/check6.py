import struct, sys
import numpy as np
sys.path.insert(0, '.')
import model
g, _ = model.font()
buf, base = model.b1996(g)
ref = np.frombuffer(open('r2_dumps/logo_1996_enter.bin', 'rb').read(), np.uint8)
import pickle, unicorn
snap = pickle.load(open(sys.argv[1], 'rb')); mem = snap['mem']
r32 = lambda a: struct.unpack_from('<I', mem, a)[0]
ref120 = np.frombuffer(mem, np.uint8, 64000, r32(0x1ede1))
for n in range(0, 160):
    cur = buf[base:base+64000]
    if np.array_equal(cur, ref120): print('t120 1996 == draw + %d blur calls' % n)
    if np.array_equal(cur, ref): print('logo entry 1996 == draw + %d blur calls' % n)
    model.f47(buf, base, 200, 320)
print('max at entry', ref.max())
for t in (11420, 11450, 11600, 11900):
    b = np.frombuffer(open(f'r2_dumps/lf_1996_t{t}.bin', 'rb').read(), np.uint8)
    buf2 = np.zeros(0xfc80, np.int64); buf2[base:base+64000] = ref
    for n in range(0, 80):
        if np.array_equal(buf2[base:base+64000], b): print(t, 'blur calls since entry', n, 'max', b.max()); break
        model.f47(buf2, base, 200, 320)
