import sys
sys.path.insert(0, '.')
import numpy as np
from PIL import Image
import model
src = model.SEG[0x16ca3:0x16ca3+2048]
q = np.zeros((64, 64), np.uint8)
for r in range(64):
    for k, b in enumerate(src[r*32:(r+1)*32]):
        q[r, 2*k] = (b >> 4) * 17; q[r, 2*k+1] = (b & 15) * 17
Image.fromarray(q).resize((256, 256), Image.NEAREST).save('rawquad.png')
print('distinct nibbles', sorted(set(np.array(list(src)) >> 4) | set(np.array(list(src)) & 15)))
