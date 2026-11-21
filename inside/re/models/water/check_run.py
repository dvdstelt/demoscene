"""Check that a run resumed from snaps/f8468 reproduces full.frames (run it without --no-frames), and print the
dumped per-frame state. usage: check_run.py run1"""
import pickle, sys
import numpy as np
sys.path.insert(0, '../..')
from sheet import open_frames

run = sys.argv[1] if len(sys.argv) > 1 else 'run1'
full, n = open_frames('../../full.frames')
mine, m = open_frames(run + '.frames')
bad = 0
for k in range(m):
    a = np.asarray(full[24 + (8468 + k) * 64768:24 + (8469 + k) * 64768])
    b = np.asarray(mine[24 + k * 64768:24 + (k + 1) * 64768])
    if not np.array_equal(a, b):
        bad += 1
print('%d records, %d differ from full.frames' % (m, bad))
D = pickle.load(open(run + '.pkl', 'rb'))
for i, f in enumerate(D['frames'][:6]):
    print(i, 'refresh', f['refresh'], 'flip', f.get('flip_refresh'), 'c', f['c'], 'seed %#x' % f['seed'],
          'scene', f.get('scene')[4:8], 'proj', [(p[0], round(p[1], 2), round(p[2], 2)) for p in f['proj'][:3]])
print('frame refreshes', [f['refresh'] for f in D['frames']][:40])
