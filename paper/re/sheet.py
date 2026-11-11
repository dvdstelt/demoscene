"""Contact sheet of .frames at given ticks: python3 sheet.py full.frames out.jpg t1 t2 ..."""
import sys
import numpy as np
from PIL import Image, ImageDraw
FR = 64000 + 768
data = np.memmap(sys.argv[1], dtype=np.uint8, mode='r')
ts = [int(t) for t in sys.argv[3:] if (int(t) + 1) * FR <= len(data)]
C = 4
sheet = Image.new('RGB', (320 * C, 200 * ((len(ts) + C - 1) // C)))
d = ImageDraw.Draw(sheet)
for i, t in enumerate(ts):
    f = data[t * FR:(t + 1) * FR]
    pal = (f[64000:].reshape(256, 3).astype(np.uint16) * 255 // 63).astype(np.uint8)
    im = Image.fromarray(pal[f[:64000]].reshape(200, 320, 3))
    sheet.paste(im, ((i % C) * 320, (i // C) * 200))
    d.text(((i % C) * 320 + 3, (i // C) * 200 + 2), str(t), fill=(255, 255, 0))
sheet.save(sys.argv[2], quality=85)
