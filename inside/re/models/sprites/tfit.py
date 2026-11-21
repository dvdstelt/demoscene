"""Which value T of the music clock M = [0x4248c4] did the frame routine use for the light and the shadow corner?

For every dumped frame of the pictures with shadows, searches T near the dump-time value for which the light's
position (+0x10..+0x18) and intensity (+0xcc), as the frame routine 0x4047a0 computes them, equal the dumped
values, and prints how often T - 4 N takes each value (N = the record that shows the frame).
usage: python3 models/sprites/tfit.py models/sprites/runs/c.dump.pkl
"""
import pickle, struct, sys
from collections import Counter
import numpy as np

frames = pickle.load(open(sys.argv[1], 'rb'))
c, s = np.cos, np.sin
cnt = Counter()
for fr in frames:
    if fr['pic'] < 3:
        continue
    lp = struct.unpack_from('<3i', fr['light']['raw'], 0x10)
    I = struct.unpack_from('<i', fr['light']['raw'], 0xcc)[0]
    if fr['pic'] == 3:
        f1, f2, g1, g2 = 0.005537098560354375, 0.004805382027871216, 0.0056625141562853904, 0.0061236987140232705
    else:
        f1, f2, g1, g2 = 0.007656967840735069, 0.004805382027871216, 0.0056625141562853904, 0.0088261253309797
    hits = []
    for T in range(fr['irqs'] - 12, fr['irqs'] + 1):
        m = (int(1750 * c(T * f1) * s(T * f2)), int(1750 * c(T * g1) * s(T * g2)),
             int(1750 * c(T * 0.00508646998982706) * s(T * 0.006648936170212766)))
        if m == lp and int(400 * s(T * 0.0033255736614566014) + 1200) == I:
            hits.append(T)
    cnt[tuple(T - 4 * fr['frame'] for T in hits)] += 1
print('T - 4 N:', cnt.most_common(8))
