import sys, pickle, struct, numpy as np
sys.path.insert(0, '/home/dennis/Downloads/acme_303/paper/re')
import unicorn
snap = lambda t: pickle.load(open('../../snaps/t%d.snap' % t, 'rb'))
s = snap(120); m = s['mem']
ptr = lambda a: struct.unpack_from('<I', m, a)[0]
tex = np.frombuffer(m, np.uint8, 65536, ptr(0x1eddd)).reshape(256, 256)
c256 = np.fromfile('cloud256.bin', np.uint8).reshape(256, 256)
print('cloud256 exact vs t120 [0x1eddd]: differing texels', int((tex != c256).sum()))
hi = np.fromfile('cloudhi.bin', np.uint16).reshape(1024, 1024).astype(np.float64) / 65535 * 255
down = hi.reshape(256, 4, 256, 4).mean(axis=(1, 3))
d = down - c256
print('cloud hi-res area-downsampled vs exact: mean diff %.2f, mean abs %.2f, max abs %.1f, corr %.4f, range hi %.1f..%.1f exact %d..%d'
      % (d.mean(), np.abs(d).mean(), np.abs(d).max(), np.corrcoef(down.ravel(), c256.ravel())[0, 1], hi.min(), hi.max(), c256.min(), c256.max()))
# tiling: compare wrap edges of hi-res
print('hi-res tiling: |row0-row1023| mean %.3f vs |row0-row1| mean %.3f' % (np.abs(hi[0]-hi[-1]).mean(), np.abs(hi[0]-hi[1]).mean()))
grain = np.frombuffer(m, np.uint8, 64000, ptr(0x1edf5))
g = np.fromfile('grain.bin', np.uint8)
print('grain exact vs t120 [0x1edf5]: differing', int((grain != g).sum()))
gf = np.fromfile('grainf.bin', np.float32)
print('grain smooth: mean %.4f (exact %.4f) var %.4f (exact %.4f) corr %.3f, floor(e+7/16)==exact: %.3f'
      % (gf.mean(), g.mean(), gf.var(), g.var(), np.corrcoef(gf, g)[0, 1], (np.floor(gf + 7/16) == g).mean()))
font = np.frombuffer(m, np.uint8, 29 * 2048, ptr(0x1ede9))
f = np.fromfile('font.bin', np.uint8)
print('font vs t120 [0x1ede9]: differing bytes', int((font != f).sum()))
# title warp grid: snapshot grid vs exact grids at ticks 110..125
grid = np.frombuffer(m, np.int32, 41 * 26 * 3, ptr(0x1edd9)).reshape(-1, 3)
G = np.fromfile('titlegrids.bin', np.int32).reshape(16, -1, 2)
for k in range(16):
    if np.array_equal(G[k][:, 0] & 0xffff, grid[:, 0] & 0xffff) and np.array_equal(G[k][:, 1] & 0xffff, grid[:, 1] & 0xffff):
        print('title grid in t120 equals warpGridExact at tick', 110 + k, '(state after IRQ of that tick)')
s8 = snap(8000); m8 = s8['mem']
grid8 = np.frombuffer(m8, np.int32, 41 * 26 * 3, struct.unpack_from('<I', m8, 0x1edd9)[0]).reshape(-1, 3)
W = np.fromfile('watergrids.bin', np.int32).reshape(16, -1, 2)
for k in range(16):
    if np.array_equal(W[k][:, 0] & 0xffff, grid8[:, 0] & 0xffff) and np.array_equal(W[k][:, 1] & 0xffff, grid8[:, 1] & 0xffff):
        print('greetings grid in t8000 equals warpGridExact at tick', 7990 + k)
osc = np.fromfile('osc.bin', np.int32).reshape(4, 72, 3)
for i, t in enumerate((120, 6100, 7000, 8000)):
    mm = snap(t)['mem'] if t != 120 else m
    rec = np.frombuffer(mm, np.int32, 72 * 3, struct.unpack_from('<I', mm, 0x1edd5)[0]).reshape(72, 3)
    print('oscillators at t%d: equal %s' % (t, np.array_equal(rec, osc[i])))
hue = np.fromfile('hue.bin', np.uint8).reshape(2, 1024, 768)
wp = np.frombuffer(m, np.uint8, 768, 0x1eaad)
ang = struct.unpack_from('<i', m, 0x14dd)[0]; sh = m[0x1505]
print('hue palette: t120 working palette (angle [0x14dd]=%d, shift %d) found at angles' % (ang, sh), [a for a in range(1024) if np.array_equal(hue[sh - 11][a], wp)])
fl = np.fromfile('fade.bin', np.int32)
import json
log = json.load(open('../fade/all.json'))
print('fade C++ vs emulator: mismatching ticks', sum(1 for t in range(12235) if fl[t] != log[str(t)][0]))
