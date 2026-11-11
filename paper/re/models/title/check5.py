import struct, sys, pickle
import numpy as np
sys.path.insert(0, '.')
import model
import unicorn
g, _ = model.font()
logo, lb = model.logo_buffer(g)
snap = pickle.load(open(sys.argv[1], 'rb')); mem = snap['mem']
r32 = lambda a: struct.unpack_from('<I', mem, a)[0]
lm = np.frombuffer(mem, np.uint8, 0x6900, r32(0x1ede5) - 0x280)
print('logo buffer match', np.array_equal(lm, logo.astype(np.uint8)), (lm != logo).sum())
for t in (11420, 11450, 11600, 11900):
    d = 'r2_dumps/'
    Z, K, s = struct.unpack('<3I', open(d + f'lf_vars_t{t}.bin', 'rb').read())
    b = np.frombuffer(open(d + f'lf_1996_t{t}.bin', 'rb').read(), np.uint8).astype(np.int64)
    pre = np.frombuffer(open(d + f'lo_pre_t{t}.bin', 'rb').read(), np.uint8)
    vga = np.frombuffer(open(d + f'lo_vga_t{t}.bin', 'rb').read(), np.uint8)
    c, v = model.logo_frame(logo, lb, b, s)
    print(t, 'Z', hex(Z), 'K', hex(K), 's', s, 'pre match', np.array_equal(c, pre), (c != pre).sum(), 'vga match', np.array_equal(v, vga), (v != vga).sum())
