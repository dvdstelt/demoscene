"""Write the name pictures, the water mask and the smoke sprite as greyscale PNG (index * 8), stacked, to argv[1]."""
import os, sys
import numpy as np
from PIL import Image
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..'))
import datfile
d = datfile.Dat(os.path.join(HERE, '..', '..', '..', 'original', 'INSIDE.DAT'))
sizes = [('kredu1', 173, 75), ('kredu7', 129, 75), ('kredu2', 146, 75), ('kredu8', 154, 72), ('kredu3', 318, 82),
         ('kredu6', 294, 75), ('wtrmask', 320, 200), ('sauhu', 64, 64)]
sheet = Image.new('L', (320 * 3, sum(h for _, _, h in sizes) * 3 + 8 * 3 * len(sizes)), 64)
y = 0
for n, w, h in sizes:
    a = np.frombuffer(d.get('credits/%s.raw' % n), np.uint8).reshape(h, w)
    im = Image.fromarray(np.minimum(a.astype(np.int32) * 8, 255).astype(np.uint8)).resize((w * 3, h * 3), Image.NEAREST)
    sheet.paste(im, (0, y)); y += h * 3 + 24
sheet.save(sys.argv[1])
