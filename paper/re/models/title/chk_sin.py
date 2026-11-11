import pickle, struct, math, sys
import unicorn
import numpy as np
snap = pickle.load(open(sys.argv[1], 'rb')); mem = snap['mem']
S2 = np.array([struct.unpack_from('<i', mem, 0x22479+4*i)[0] for i in range(1024)])
ideal = np.array([round(65536*math.sin(2*math.pi*k/1024)) for k in range(1024)])
d = S2 - ideal
print('max abs diff', abs(d).max(), 'nonzero', (d != 0).sum())
f = np.float32(0.00306796)
step = float(struct.unpack('<f', struct.pack('<f', 0.0030679615))[0])
print(struct.unpack('<f', bytes(mem[0x1e464:0x1e468]))[0], repr(step))
x = 0.0; alt = []
st = struct.unpack('<f', bytes(mem[0x1e464:0x1e468]))[0]
for k in range(1024):
    alt.append(int(round(math.sin(x)*65536))); x += st; x += st
print('accumulated float32-step model max diff', abs(S2 - np.array(alt)).max())
