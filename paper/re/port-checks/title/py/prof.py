import sys
import numpy as np
from PIL import Image
sys.path.insert(0, 'py')
FR = 64000 + 768
frames = np.memmap('/home/dennis/Downloads/acme_303/paper/re/full.frames', dtype=np.uint8, mode='r')
t = int(sys.argv[1]); y = float(sys.argv[2]); x0, x1 = map(int, sys.argv[3:5]); d = sys.argv[5] if len(sys.argv) > 5 else 'big'
f = np.asarray(frames[t*FR:(t+1)*FR]); idx = f[:64000].reshape(200, 320); pal = f[64000:].reshape(256, 3)
im = np.asarray(Image.open('%s/shot_%05d.png' % (d, t)).convert('RGB')).astype(float)
sx = im.shape[1] / 320; sy = im.shape[0] / 200

row = im[int((y + 0.5) * sy), :, 0]
print('remake R  (pixel centres):', ' '.join('%3d' % row[int((x + 0.5) * sx)] for x in range(x0, x1)))
print('orig R                   :', ' '.join('%3d' % (int(pal[idx[int(y), x], 0]) * 255 // 63) for x in range(x0, x1)))
