"""Font sheet at code32 0x3cec2: compare the file with the snapshots, print statistics."""
import os, sys, pickle
import numpy as np
RE = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, RE)
import unicorn  # noqa: needed to unpickle
img = open(os.path.join(RE, 'unpacked/korso2.bin'), 'rb').read()
B = 0xec0; LIN = 0x8fc0
A, W, H = 0x3cec2, 320, 38
f = np.frombuffer(img[B + A:B + A + W * (H + 4)], np.uint8).reshape(H + 4, W)
print('file: nonzero rows', [r for r in range(H + 4) if f[r].any()])
print('values', np.unique(f[:H], return_counts=True))
for name in ['part_2c26f', 'part_38520', 'part_4d370']:
    s = pickle.load(open(os.path.join(RE, 'snaps', name + '.snap'), 'rb'))
    m = np.frombuffer(s['mem'][LIN + A:LIN + A + W * (H + 4)], np.uint8).reshape(H + 4, W)
    print(name, 'differs from file at', np.count_nonzero(m != f), 'bytes')
    w = np.frombuffer(s['mem'][LIN + 0x4c8c2:LIN + 0x4c8c2 + 40], np.uint8)
    print('  widths', list(w))
