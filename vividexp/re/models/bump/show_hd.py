"""The original frame (model, 320x200, nearest-neighbour 4x) next to the continuous sketch frame_hd at 1280x800.
usage: python3 show_hd.py bump/bump1.ini N_REFRESH B_REFRESH out.png"""
import sys

import numpy as np
from PIL import Image

import bump

fx = bump.Bump(sys.argv[1])
n, bref = int(sys.argv[2]), int(sys.argv[3])
st = bump.State(0.0, 0.0)
st.step(bref - n, fx)          # wave refreshes before this Run (DOOMSDAY: SUOMI + ICE)
st.ang_x = st.ang_y = 0.0
st.step(n, fx)
idx = fx.frame(st)
if fx.type == 1:
    pal = fx.light_pal
else:
    pal = np.frombuffer(bump.load('pal/phong.pal' if 'phong.pcx' in fx.ini['light'] else 'pal/phong2.pal'),
                        np.uint8).reshape(256, 3)
lut = (pal.astype(np.uint32) * 255 + 31) // 63
orig = Image.fromarray(lut[idx].astype(np.uint8)).resize((1280, 800), Image.NEAREST)
hd = Image.fromarray((np.clip(bump.frame_hd(fx, n, bref), 0, 1) * 255 + 0.5).astype(np.uint8))
sheet = Image.new('RGB', (2560, 800))
sheet.paste(orig, (0, 0))
sheet.paste(hd, (1280, 0))
sheet.save(sys.argv[4])
