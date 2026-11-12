"""Compare p5model frames with the engine's frame buffers dumped by hookrun.py (npz from a p5 run); also checks the
bump table formula (bump_table(phase)) against the table the engine used."""
import sys, os
import numpy as np
from PIL import Image
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import p5model
d = np.load(sys.argv[1])
outdir = sys.argv[2] if len(sys.argv) > 2 else None
bg = p5model.background()
pal = np.load(os.path.join(HERE, 'out', 'p5_pal.npy'))
import pickle
snap = pickle.load(open(os.path.join(HERE, '..', '..', 'snaps', 'part_p5.snap'), 'rb'))
pal[:16] = np.frombuffer(bytes(snap['vga']['dac']), np.uint8).reshape(256, 3)[:16]
rgb = ((pal.astype(np.uint16) * 255 + 31) // 63).astype(np.uint8)
# bump table formula vs the engine's tables (phase is not recorded: search the phase that reproduces each table)
tabs = {tuple(p5model.bump_table(ph)): ph for ph in range(0, 0x800, 2)}
ok = sum(1 for t in d['tab'] if tuple(int(v) for v in t) in tabs)
print('bump tables reproduced by the formula: %d of %d rows' % (ok, len(d['tab'])))
for k in sorted(int(n[3:]) for n in d.files if n.startswith('fb_')):
    ref = d['fb_%d' % k]
    lab = int(d['label'][k]); lab = lab - 0x10000 if lab & 0x8000 else lab
    o1 = d['o1'][k]
    labelseg = 0x248f if int(o1[0x2a]) == 0 else 0x258a
    mod = p5model.render_frame(d['o0'][k], o1, lab, labelseg, d['tab'][k].astype(np.int64), bg)
    diff = mod != ref
    obj = ref >= 16
    print('frame %d retr %d mat %04x: %d / 64000 pixels differ, object pixels %d, differing %d' % (
        d['frame'][k], d['retr'][k], int(o1[0x2a]) | int(o1[0x2b]) << 8, diff.sum(), obj.sum(), (diff & obj).sum()))
    if outdir:
        im = np.concatenate([rgb[ref.reshape(200, 320)], rgb[mod.reshape(200, 320)],
                             np.where(diff.reshape(200, 320)[..., None], 255, 0).astype(np.uint8).repeat(3, 2)], 1)
        Image.fromarray(im).save(os.path.join(outdir, 'p5cmp_%d.png' % d['frame'][k]))
