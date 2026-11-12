"""Writes the tunnel's polar map [0xca0] and projection table [0xcc0] from snapshot f11498_11e8 to runs/*.npy, for
tunnel_model.py to compare against."""
import numpy as np

from common import *

s = load_snap(os.path.join(RE, 'snaps', 'f11498_11e8.snap'))
mem = s['mem']
os.makedirs(os.path.join(HERE, 'runs'), exist_ok=True)
pm, pt = rd32(0xca0, mem), rd32(0xcc0, mem)
np.save(os.path.join(HERE, 'runs', 'tunnel_map.npy'), np.frombuffer(mem[pm:pm + 128000], '<u2'))
np.save(os.path.join(HERE, 'runs', 'tunnel_depth.npy'), np.frombuffer(mem[pt:pt + 0x10000], np.uint8))
print('polar map at %#x, projection table at %#x' % (pm, pt))
