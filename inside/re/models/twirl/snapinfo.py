"""Print the twirl part's state from a snapshot: pointers, parameters, phases, allocator flag."""
import pickle, struct, sys, os
import unicorn  # noqa: F401  (needed to unpickle)
HERE = os.path.dirname(os.path.abspath(__file__))
RE = os.path.dirname(os.path.dirname(HERE))
snap = sys.argv[1] if len(sys.argv) > 1 else os.path.join(RE, 'snaps', 'f10574.snap')
s = pickle.load(open(snap, 'rb'))
m = s['mem']
d32 = lambda a: struct.unpack_from('<I', m, a)[0]
f32 = lambda a: struct.unpack_from('<f', m, a)[0]
print('keys', list(s.keys()), 'eip', hex(s['eip']))
for a, n in [(0x4b6d3c, 'picture'), (0x4b6d40, 'picture+0x8000'), (0x4b6d54, 'palette'), (0x4b6d38, 'twirl table'),
             (0x4b6d28, 'pre-table'), (0x4b6d30, 'mem tag'), (0x4b6d4c, 'dest'), (0x42296c, 'picture idx'),
             (0x422970, 'param set'), (0x4b6ed8, 'alloc clears'), (0x424888, 'frame rt'), (0x4248b4, 'tick rt'),
             (0x4248c8, 'part ticks'), (0x424894, 'dat block'), (0x42277c, 'debug text')]:
    print(f'{n:16s} [{a:#x}] = {d32(a):#x}')
for a in range(0x4b6d08, 0x4b6d5c, 4):
    print(f'[{a:#x}] = {f32(a)!r}  ({d32(a):#010x})')
print('param sets:')
for k in range(5):
    print(k, [f32(0x4b6c68 + 0x20 * k + 4 * i) for i in range(8)])
