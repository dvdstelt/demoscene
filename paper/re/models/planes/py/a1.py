from snap import *
for t in (6100, 7000, 8700, 9500):
    m = M(t)
    print('tick', t, 'keys', list(m.s.keys()), 'eip %x' % m.s['eip'])
    for name, a in [('tex 1eddd',0x1eddd),('shade 1eded',0x1eded),('grid 1edd9',0x1edd9),('wave 1edd5',0x1edd5),('back 1edf9',0x1edf9),('vga 1edf1',0x1edf1),('hist 2449d',0x2449d),('obj 6816',0x6816),('flock 33c0',0x33c0),('1ee05',0x1ee05),('1ee09',0x1ee09),('1ee0d',0x1ee0d)]:
        print('  %s = %x' % (name, m.d(a)))
    print('  floats 23a8..23e4', ['%.4g' % x for x in m.ff(0x23a8, 0x10)])
    print('  cam int', m.ii(0x1ea9d, 3), 'k 4843', m.i(0x4843), 'pos', m.ii(0x6479, 3), 'idx', m.i(0x24499))
    print('  1eaa9', m.i(0x1eaa9), 'cnt', m.i(0x1ea8b), 'tgt', m.i(0x1ea8f), '1edc1', m.i(0x1edc1), '217d9', m.i(0x217d9), 'lvl', m.i(0x1edb5), m.i(0x1edb9), m.i(0x1edbd))
    print('  V 1e468', m.ii(0x1e468, 9), 'L', m.ii(0x1e48c, 3))
    print('  flock', m.ii(0x3390, 12), m.ii(0x33c0, 4))
