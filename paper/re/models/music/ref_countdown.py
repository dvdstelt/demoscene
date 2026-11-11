import sys
sys.dont_write_bytecode = True
sys.path.insert(0, '/home/dennis/Downloads/acme_303/paper/re')
import player_ref as P
seg = open('/home/dennis/Downloads/acme_303/paper/re/paper_img.bin', 'rb').read()[P.C32:]
tune = P.Tune(seg)
gus = P.GF1(tune.dram)
pl = P.Player(tune, gus, seg)
pl.sound_init()
out = []
facc = 0.0
for t in range(12235):
    gus.tick = t
    if t == 104: pl.on = True
    pl.irq()
    out.append('%d %d %d\n' % (t, pl.row, pl.count))
    facc += gus.frame_rate() / P.PIT_HZ
    n = int(facc); facc -= n
    gus.advance(n)
open(sys.argv[1], 'w').write(''.join(out))
