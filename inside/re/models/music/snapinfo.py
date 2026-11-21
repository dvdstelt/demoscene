"""Print DSMI state from a snapshot: driver entry points, the CDI table, the module header.
python3 snapinfo.py ../../snaps/f38.snap"""
import pickle, struct, sys
import unicorn  # noqa: F401
snap = pickle.load(open(sys.argv[1], 'rb'))
mem = snap['mem']
def d32(a): return struct.unpack_from('<I', mem, a)[0]
def d16(a): return struct.unpack_from('<H', mem, a)[0]
print('SDIInitDevice %x SDIStartOutput %x SDIStopOutput %x' % (d32(0x4249f4), d32(0x4249f8), d32(0x4227a8)))
print('cdi devices', mem[0x4237fa], 'channel->device', list(mem[0x4233ba:0x4233ba + 32]))
for k in range(mem[0x4237fa]):
    t = [d32(0x4234ba + 0x68 * k + 4 * i) for i in range(26)]
    print('cdi table', k, ' '.join('%02x:%x' % (4 * i, v) for i, v in enumerate(t)))
print('module ptr %x' % d32(0x4227a4))
m = d32(0x4227a4)
print('module header', mem[m:m + 0x5c].hex(' '))
print('AMP copy    ', mem[0x4229f0:0x4229f0 + 0x5c].hex(' '))
print('DSMI time %d step %d last tick %d tempo-div %d' % (d32(0x4b9148), d32(0x4b9144), d32(0x4233aa), d16(0x4233a8)))
