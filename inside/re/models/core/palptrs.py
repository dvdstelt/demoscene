"""Base palettes the script routines pass to 0x400364, mapped to INSIDE.DAT files (from snapshot f38)."""
import os, pickle, struct, sys
HERE = os.path.dirname(os.path.abspath(__file__))
RE = os.path.join(HERE, '..', '..')
sys.path.insert(0, RE)
import unicorn  # noqa: F401  (unpickling needs it)
import datfile
s = pickle.load(open(os.path.join(RE, 'snaps', 'f38.snap'), 'rb'))
m = s['mem']
d = datfile.Dat(os.path.join(RE, '..', 'original', 'INSIDE.DAT'))
blk = struct.unpack_from('<I', m, 0x424894)[0]
byoff = {o: n for n, (o, sz) in d.files.items()}
def rd(a): return struct.unpack_from('<I', m, a)[0]
def name(p):
    o = p - blk
    return byoff.get(o, 'not a file start (ptr %#x, block+%#x)' % (p, o))
print('data block at %#x' % blk)
for what, a in [('intro 0x403811 [0x425578]', 0x425578), ('logo 0x403169 [0x425538]', 0x425538),
                ('credits 0x4073e1 [0x426448]', 0x426448), ('flight 0x402c51 [0x4227b4]', 0x4227b4),
                ('water 0x407fb9 [0x426614]', 0x426614), ('temple start 0x40464a', None),
                ('twirl [0x4b6d54]', 0x4b6d54)] + [('sprites [0x425f70+4*%d]' % i, 0x425f70 + 4 * i) for i in range(6)]:
    if a is None:
        continue
    p = rd(a)
    print('%-34s %#x  %s' % (what, p, name(p)))
