import sys, numpy as np
from PIL import Image
FR = 64768
data = np.memmap('/home/dennis/Downloads/acme_303/paper/re/full.frames', dtype=np.uint8, mode='r')
out = []
for spec in sys.argv[2:]:
    t, x0, y0, w, h = map(int, spec.split(','))
    f = data[t * FR:(t + 1) * FR]
    pal = (f[64000:].reshape(256, 3).astype(np.uint16) * 255 // 63).astype(np.uint8)
    im = pal[f[:64000]].reshape(200, 320, 3)[y0:y0 + h, x0:x0 + w]
    out.append(Image.fromarray(im).resize((w * 4, h * 4), Image.NEAREST))
W = sum(i.width for i in out); H = max(i.height for i in out)
sheet = Image.new('RGB', (W, H)); x = 0
for i in out: sheet.paste(i, (x, 0)); x += i.width
sheet.save(sys.argv[1])
