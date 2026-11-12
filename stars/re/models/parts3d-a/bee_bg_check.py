"""Is file 27 (the bee's background) a frame of the cloud effect? Compare in RGB against cloud_model.draw."""
import numpy as np
from common import out, file
from cloud_model import draw

T = np.load(out('cloud_table.npy'))
f27 = np.frombuffer(file(27), np.uint8).reshape(200, 320)
p26 = np.frombuffer(file(26), np.uint8).reshape(256, 3).astype(int)
p31 = np.frombuffer(file(31), np.uint8).reshape(256, 3).astype(int)
rgb27 = p26[f27]
best = []
for n in range(0, 700):
    for S in (1024 * n, 1024 * n + 1):
        img = draw(T, S)
        rgb = p31[img]
        d = np.abs(rgb[1:198] - rgb27[1:198]).sum(axis=2)
        best.append((float(d.mean()), S / 1024, int((d > 6).sum())))
best.sort()
print(best[:8])
