import sys, pickle, glob, unicorn, struct
for f in sorted(glob.glob(sys.argv[1] + '/../snaps/t*.snap'), key=lambda s: int(s.split('/t')[-1].split('.')[0])):
    sn = pickle.load(open(f, 'rb'))
    mem = sn['mem']
    g = lambda a: struct.unpack_from('<i', mem, a)[0]
    print(sn['tick'], 'blur budget', g(0x1edad), 'ea94 mode', mem[0x1ea94], '42ac', mem[0x42ac], '1eded', hex(g(0x1eded)), '1eddd', hex(g(0x1eddd)), 'heap', hex(g(8)))
