"""The random generator 0x4af10: s = rol32((s ^ 0x78d47e5d) * 0xd413d473, 18), seed dword [0x4af35] = 0x00007ea9
in the image (never reseeded); returns s mod EAX (unsigned). Checks the state in the snapshots: how many calls lie
between the program start and the particle parts."""
import pickle, struct, sys, os
import unicorn  # noqa: F401  (needed to unpickle the snapshots)

RE = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
MASK = 0xffffffff


def step(s):
    s = ((s ^ 0x78d47e5d) * 0xd413d473) & MASK
    return ((s << 18) | (s >> 14)) & MASK


def rand(state, n):
    state[0] = step(state[0])
    return state[0] % n


def calls_between(a, b, limit=10_000_000):
    s = a
    for i in range(limit):
        if s == b: return i
        s = step(s)
    return None


if __name__ == '__main__':
    seed = struct.unpack_from('<I', open(os.path.join(RE, 'unpacked', 'stars_img.bin'), 'rb').read(), 0x4af35)[0]
    print('seed in the image: %#x' % seed)
    prev = seed
    for name in ('f18451_133ad', 'f18997_1528c', 'f21956_133f1', 'f23433_10b26', 'f26359_4a006'):
        m = pickle.load(open(os.path.join(RE, 'snaps', name + '.snap'), 'rb'))['mem']
        s = struct.unpack_from('<I', m, 0x4af35)[0]
        print(f'{name}: state {s:#010x}, {calls_between(prev, s)} calls since the previous one')
        prev = s
