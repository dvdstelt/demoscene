from mm import M
for t in ['img', 1150, 1250, 2000]:
    m = M(t)
    print('==', t)
    for name, a in [('55d2',0x55d2),('55d6',0x55d6),('55da',0x55da),('55de',0x55de),('5047',0x5047),('504b',0x504b),('504f',0x504f),
                    ('5053',0x5053),('5057',0x5057),('505b',0x505b),('505f',0x505f),('1ea95',0x1ea95),('482b',0x482b),('466b',0x466b),
                    ('4827',0x4827),('1edb5',0x1edb5),('1edbd',0x1edbd),('1edb9',0x1edb9),('1edad',0x1edad),('1edc1',0x1edc1),('1ea8b',0x1ea8b),('1ea8f',0x1ea8f),
                    ('1e48c',0x1e48c),('1e490',0x1e490),('1e494',0x1e494),('5de4',0x5de4),('1ea99',0x1ea99),('5de8',0x5de8),('5dec',0x5dec),('5df0',0x5df0),('5df4',0x5df4),('5df8',0x5df8),
                    ('1ea9d',0x1ea9d),('1eaa1',0x1eaa1),('1eaa5',0x1eaa5),('8',8),('1edf5',0x1edf5),('1edf9',0x1edf9),('1eded',0x1eded),('1ee05',0x1ee05),('1ee09',0x1ee09),('1ee0d',0x1ee0d),('1ede1',0x1ede1),('5063',0x5063),('5067',0x5067),('482f',0x482f)]:
        print('%s=%d/%x' % (name, m.i(a), m.d(a)), end='  ')
    print(' 506b=', m.by(0x506b), '1ea94=', m.by(0x1ea94), 'aa0', m.by(0xaa0), m.by(0xaa1))
