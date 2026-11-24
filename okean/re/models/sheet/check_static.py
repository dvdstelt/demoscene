"""Which of the part's inputs are static in the file and which were generated before the part starts."""
import os, pickle
import numpy as np
import unicorn  # noqa
HERE = os.path.dirname(os.path.abspath(__file__))
RE = os.path.dirname(os.path.dirname(HERE))
snap = pickle.load(open(os.path.join(RE, 'snaps', 'part_5334f.snap'), 'rb'))
mem = snap['mem']
binf = open(os.path.join(RE, 'unpacked', 'korso2.bin'), 'rb').read()
C32 = 0x8fc0
def cmp(name, a, n):
    s = np.frombuffer(bytes(mem[C32 + a:C32 + a + n]), np.uint8)
    f = np.frombuffer(binf[0xec0 + a:0xec0 + a + n], np.uint8)
    d = np.nonzero(s != f)[0]
    print('%-10s %06x %6d bytes: %d differ%s; file nonzero %d' % (name, a, n, len(d),
          (' (first %x last %x)' % (a + d[0], a + d[-1])) if len(d) else '', np.count_nonzero(f)))
cmp('sine', 0x24a3c, 5121 * 4)
cmp('font', 0x3cec2, 320 * 38)
cmp('grid', 0x4decd, 256 * 12)
cmp('edges', 0x4eacd, 480 * 8)
cmp('pals', 0x526cd, 1536)
cmp('rowtab', 0x52ccd, 800)
print('len bin', hex(len(binf) - 0xec0))
s = np.frombuffer(bytes(mem[C32 + 0x24a3c:C32 + 0x24a3c + 5121 * 4]), '<i4')
import math
k = np.arange(5121)
for amp in (1022, 1023, 1024):
    ref = amp * np.sin(2 * np.pi * k / 4096)
    print(amp, 'max |s - ref|', np.abs(s - ref).max(), 'trunc eq', (np.trunc(ref) == s).mean(), 'round eq', (np.round(ref) == s).mean())
print(s[1020:1030], s[2044:2054], s[3068:3078], s[4092:4100], s[5116:5121])
