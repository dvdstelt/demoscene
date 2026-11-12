"""Compare p3model frames with the engine's frame buffers dumped by hookrun.py (npz from a p3 run)."""
import sys, os
import numpy as np
from PIL import Image
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import p3model
d = np.load(sys.argv[1])
outdir = sys.argv[2] if len(sys.argv) > 2 else None
pal = np.load(os.path.join(HERE, 'out', 'p3_pal.npy'))
rgb = ((pal.astype(np.uint16) * 255 + 31) // 63).astype(np.uint8)
for k in sorted(int(n[3:]) for n in d.files if n.startswith('fb_')):
    ref = d['fb_%d' % k]
    lab = int(d['label'][k]); lab = lab - 0x10000 if lab & 0x8000 else lab
    mod = p3model.render_frame(d['o0'][k], lab)
    diff = mod != ref
    obj = ref >= 128
    print('frame %d retr %d: %d / 64000 pixels differ (%.3f%%), object pixels %d, object pixels differing %d' % (
        d['frame'][k], d['retr'][k], diff.sum(), diff.mean() * 100, obj.sum(), (diff & obj).sum()))
    if outdir:
        im = np.concatenate([rgb[ref.reshape(200, 320)], rgb[mod.reshape(200, 320)],
                             np.where(diff.reshape(200, 320)[..., None], 255, 0).astype(np.uint8).repeat(3, 2)], 1)
        Image.fromarray(im).save(os.path.join(outdir, 'p3cmp_%d.png' % d['frame'][k]))
