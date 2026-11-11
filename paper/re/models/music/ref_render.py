# Float64 GF1 mix exactly as player_ref.py --wav computes it (before its peak normalisation), saved as .npy.
import sys
sys.dont_write_bytecode = True
sys.path.insert(0, '/home/dennis/Downloads/acme_303/paper/re')
import numpy as np
import player_ref as P

out = sys.argv[1]
seg = open('/home/dennis/Downloads/acme_303/paper/re/paper_img.bin', 'rb').read()[P.C32:]
tune = P.Tune(seg)
gus = P.GF1(tune.dram)
pl = P.Player(tune, gus, seg)
gus.prepare_render()
facc = 0.0
mix = []
gus.tick = 0
pl.sound_init()
for t in range(0, 12235):
    gus.tick = t
    if t == 104:
        pl.on = True
    pl.irq()
    facc += gus.frame_rate() / P.PIT_HZ
    n = int(facc)
    facc -= n
    buf = [np.zeros(n), np.zeros(n)]
    gus.advance(n, buf)
    mix.append(np.stack(buf, 1))
pcm = np.concatenate(mix)
np.save(out, pcm)
print('frames', len(pcm), 'peak %.17g' % np.abs(pcm).max())
