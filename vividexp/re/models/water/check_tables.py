"""Check wdata.py's tables against the program image and the snapshot snaps2/f21.snap (WaterEffect's first frame)."""
import struct
import sys

import numpy as np

import wdata
import wsnap

sys.path.insert(0, wdata.RE)
import rep  # noqa: E402

D = wdata.load()
m = wsnap.snap('snaps2/f21.snap')

# sine table [0x844d20]: 1025 dwords
tab = [m.s32(m.rd32(0x844d20) + 4 * k) for k in range(1025)]
print('sin table', 'exact' if tab == D['sin'] else 'DIFF %d' % sum(a != b for a, b in zip(tab, D['sin'])), tab[:3], tab[1020:])

# env table (static, 0x825428 +- 512)
img = {i: struct.unpack('<h', rep.rd(0x825428 + 2 * i, 2))[0] for i in range(-256, 257)}
diff = [(i, img[i], D['envtab'][i]) for i in img if img[i] != D['envtab'][i]]
print('env table', 'exact' if not diff else 'DIFF %d %s' % (len(diff), diff[:8]))

# bump table [0x833dd8] (256 x 256 words)
bt = m.blk(m.rd32(0x833dd8), 0x20000).reshape(256, 256, 2)
print('bump table', 'exact' if (bt == D['bump']).all() else 'DIFF %d' % (bt != D['bump']).sum())

# env halves: [0x833ddc] left, [0x833de4] clean right
for a, k in ((0x833ddc, 0), (0x833de4, 1)):
    e = m.blk(m.rd32(a), 65536).reshape(256, 256)
    print('env half', k, 'exact' if (e == D['env'][k]).all() else 'DIFF')

# palette 0x833abc: 0, 0x100 (word), 768 bytes
pal = m.blk(0x833abf, 768).reshape(256, 3)
print('palette', m.rd8(0x833abc), m.rd16(0x833abd), 'exact' if (pal == D['pal']).all() else 'DIFF')

# background [0x833dec] -> +4 image (+8 pixels)
bk = m.blk(m.rd32(0x8336b0), 64000).reshape(200, 320)
print('background', 'exact' if (bk == D['back']).all() else 'DIFF')

# splines: solid channels [0x833ea0 + 4c], transparent [0x833eb8 + 4c]: keys x, y, y2, u
for base, name in ((0x833ea0, 'solid'), (0x833eb8, 'trans')):
    bad = 0
    for c in range(6):
        p = m.rd32(base + 4 * c)
        ch = D[name][c]
        for k in range(24):
            x, y, y2, u = (m.s32(p + 16 * k + 4 * j) for j in range(4))
            if (x, y, y2) != (ch['x'][k], ch['y'][k], ch['y2'][k]):
                bad += 1
                if bad < 4:
                    print('  ', name, c, k, (x, y, y2, u), (ch['x'][k], ch['y'][k], ch['y2'][k], ch['u'][k]))
    print('spline', name, 'exact' if not bad else 'DIFF %d' % bad)
