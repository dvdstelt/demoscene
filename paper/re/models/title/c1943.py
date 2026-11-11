import pickle, struct, sys
import unicorn
import numpy as np
from PIL import Image
snap = pickle.load(open(sys.argv[1], 'rb')); mem = snap['mem']
r32 = lambda a: struct.unpack_from('<I', mem, a)[0]
m = np.frombuffer(mem, np.uint8, 64000, r32(0x1edf5)).reshape(200, 320)
print('1edf5 range', m.min(), m.max(), np.bincount(m.reshape(-1))[:70].nonzero()[0][:20])
Image.fromarray((m.astype(int)*4).clip(0,255).astype(np.uint8)).save('edf5.png')
base = r32(0x1eddd) + 0x20000
T1 = np.frombuffer(mem, np.uint8, 256*256, base).reshape(256, 256)
out = T1[m, 0x3e]
print('0x1943 result range', out.min(), out.max())
