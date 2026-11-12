"""Print the object list (camera slot + objects), the material globals and the segment tables of a dump.
usage: python3 models/engine/objects.py <part> <dump.npz>"""
import os, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from dumpview import Dump
from engine_model import PARTS

part, path = sys.argv[1], sys.argv[2]
d = Dump(path, part)
c = PARTS[part]
o = c['objs']
print('%s frame %d: retraces %d, attr_c %d attr_b %d, env segs %s tex segs %s' % (
    part, d.frame, d.cs32(c['retraces']), d.u8(0x10, c['attr_c']), d.u8(0x10, c['attr_b']),
    ['%04x' % d.seg_of(d.cs16(c['segenv'] + 2 * i)) for i in range(4)],
    ['%04x' % (d.seg_of(d.cs16(c['segtex'] + 2 * i)) if d.cs16(c['segtex'] + 2 * i) else 0) for i in range(4)]))
while True:
    r = d.b(0x10, o, 0x3a)
    s = struct.unpack_from('<H', r, 0)[0]
    if s == 0:
        break
    print('  %04x seg %s off %d f %d pos %s vel %s ang %s dang %s attr %04x cull %d pivot %s' % (
        o, 'cam ' if s == 0xffff else '%04x' % d.seg_of(s), struct.unpack_from('<h', r, 2)[0],
        struct.unpack_from('<h', r, 4)[0], struct.unpack_from('<3i', r, 6), struct.unpack_from('<3i', r, 0x12),
        struct.unpack_from('<3H', r, 0x1e), struct.unpack_from('<3h', r, 0x24), struct.unpack_from('<H', r, 0x2a)[0],
        struct.unpack_from('<h', r, 0x2c)[0], struct.unpack_from('<3i', r, 0x2e)))
    o += 0x3a
