"""Hex dump helper: peek.py <part> <seg:off|fileoff> <len> [w|b|d]"""
import sys, struct, os
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
import rep
v = rep.View(sys.argv[1])
a = sys.argv[2]
if ':' in a:
    s, o = a.split(':'); x = int(s, 16) * 16 + int(o, 16)
else:
    x = int(a, 16)
n = int(sys.argv[3], 16) if sys.argv[3].startswith('0x') else int(sys.argv[3])
mode = sys.argv[4] if len(sys.argv) > 4 else 'b'
img = v.img
for r in range(0, n, 16):
    row = img[x + r:x + min(n, r + 16)]
    if mode == 'w':
        vals = ' '.join('%04x' % struct.unpack_from('<H', row, i)[0] for i in range(0, len(row) - 1, 2))
    elif mode == 'd':
        vals = ' '.join('%08x' % struct.unpack_from('<I', row, i)[0] for i in range(0, len(row) - 3, 4))
    else:
        vals = ' '.join('%02x' % b for b in row)
    print('%06x: %s' % (x + r, vals))
