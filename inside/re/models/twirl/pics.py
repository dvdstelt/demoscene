"""The twirl's five pictures: compare INSIDE.DAT with the loaded block in the snapshot f10574 and write PNGs
(256x256, through their palettes) to models/twirl/pic_*.png for viewing (generated from the original: not in Git)."""
import os, pickle, struct
import numpy as np
from PIL import Image
import unicorn  # noqa: F401
import sys
HERE = os.path.dirname(os.path.abspath(__file__))
RE = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, RE)
import datfile
d = datfile.Dat(os.path.join(RE, '..', 'original', 'INSIDE.DAT'))
s = pickle.load(open(os.path.join(RE, 'snaps', 'f10574.snap'), 'rb'))
m = s['mem']
blk = struct.unpack_from('<I', m, 0x424894)[0]
pics = [('flower/jeesus1.raw', 'flower/jeesus.pal'), ('nigga/nigga2.raw', 'nigga/nigga2.pal'),
        ('nigga/nigga4.raw', 'nigga/nigga4.pal'), ('flower/jeesus2.raw', 'flower/jeesus.pal'),
        ('flight/river.raw', 'flight/map.pal')]
for k, (raw, pal) in enumerate(pics):
    for name in (raw, pal):
        off, size = d.files[name]
        a = bytes(d.get(name)); b = bytes(m[blk + off: blk + off + size])
        diff = [i for i in range(size) if a[i] != b[i]]
        print(f'{name:20s} off {off:#x} size {size} differs in memory at {len(diff)} bytes {diff[:10]}')
    p = np.frombuffer(bytes(d.get(raw)), np.uint8).reshape(256, 256)
    P = np.frombuffer(bytes(d.get(pal)), np.uint8).reshape(256, 3).astype(np.float32)
    rgb = (np.minimum(P, 63) * 255 / 63 + 0.5).astype(np.uint8)[p]
    Image.fromarray(rgb).resize((512, 512), Image.NEAREST).save(os.path.join(HERE, f'pic_{k}.png'))
    used = np.unique(p)
    print(f'   indices used: {len(used)} ({used.min()}..{used.max()}); palette entry 0 = {P[0]}')
