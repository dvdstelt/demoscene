"""The twirl's phases at the temple snapshot (f11276) against the per-refresh model (frame.schedule) and the
closed form; and the event state (picture index, parameter set)."""
import os, pickle, struct, sys
import unicorn  # noqa: F401
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import frame
s = pickle.load(open(os.path.join(frame.RE, 'snaps', 'f11276.snap'), 'rb'))
m = s['mem']
f32 = lambda a: struct.unpack_from('<f', m, a)[0]
snap = (f32(0x4b6d58), f32(0x4b6d2c), f32(0x4b6d50), f32(0x4b6d34))
print('snapshot  s58 s2c p1 p2 =', snap, ' picture', struct.unpack_from('<I', m, 0x42296c)[0], 'set',
      struct.unpack_from('<I', m, 0x422970)[0])
states, sets = frame.schedule([(138, 2), (269, 3), (269, 4), (554, 0)])
for n in range(698, 706):
    if states[n] == snap:
        print('model state after', n, 'tick steps matches exactly')
E1, E2, E3 = 138, 269, 554
cl = lambda x, a, b: max(a, min(b, x))
def closed(n):
    s58 = max(0, n - E3 + 1)
    s2c = -2 * cl(n - E1 + 1, 0, E2 - E1) + 3 * cl(n - E2 + 1, 0, E3 - E2)
    p1 = -0.04 * min(n, E1 - 1) + 0.08 * cl(n - E2 + 1, 0, E3 - E2) + 0.12 * max(0, n - E3 + 1)
    p2 = 0.04 * min(n, E1 - 1) + 0.02 * cl(n - E1 + 1, 0, E2 - E1)
    return s58, s2c, p1, p2
for n in (100, 137, 200, 268, 400, 553, 600, 701):
    print(n, 'model', tuple(round(x, 6) for x in states[n]), 'closed', tuple(round(x, 6) for x in closed(n)))
