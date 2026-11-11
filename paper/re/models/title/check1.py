import pickle, struct, sys
import unicorn
import numpy as np
sys.path.insert(0, '.')
import model
snap = pickle.load(open(sys.argv[1], 'rb'))
mem = snap['mem']
r32 = lambda a: struct.unpack_from('<I', mem, a)[0]
S2 = np.array([struct.unpack_from('<i', mem, 0x22479+4*i)[0] for i in range(1024)])
print('sine table matches model:', np.array_equal(S2, model.S), S2[:5], S2[256])
model.S = S2
g, end = model.font()
fm = np.frombuffer(mem, np.uint8, 0xe800, r32(0x1ede9))
ok = all(fm[i*2048] == g[i][0] and np.array_equal(fm[i*2048+1:i*2048+1921].reshape(32,60), g[i][1]) for i in range(29))
print('font matches:', ok, 'rle end', hex(end), 'size', end - 0x18ed7)
t = model.tex()
tm = np.frombuffer(mem, np.uint8, 65536, r32(0x1eddd)).reshape(256,256)
print('texture matches:', np.array_equal(t, tm), 'diff count', (t != tm).sum(), 'max', t.max(), tm.max())
t1, t2 = model.shade_tables()
base = r32(0x1eddd) + 0x20000
m1 = np.frombuffer(mem, np.uint8, 65*256, base).reshape(65,256)
m2 = np.frombuffer(mem, np.uint8, 65*256, base+0x10000).reshape(65,256)
print('t1', np.array_equal(t1, m1), 't2', np.array_equal(t2, m2))
ang = r32(0x14dd); lvl = r32(0x1edb5); sh = mem[0x1505]
pal, base_rgb = model.palette(ang - 1, sh)   # callback incremented after building
pm = np.frombuffer(snap['pal'], np.uint8).reshape(256,3)
pf = model.fade(pal, lvl)
print('palette angle', ang, 'lvl', hex(lvl), 'sh', sh, 'base', base_rgb, 'match', np.array_equal(pf, pm), (pf != pm).sum())
pal2 = np.frombuffer(mem, np.uint8, 768, 0x1eaad).reshape(256,3)
print('raw palette match', np.array_equal(pal2, pal), (pal2 != pal).sum())
