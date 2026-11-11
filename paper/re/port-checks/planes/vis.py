import numpy as np, pickle, sys
from PIL import Image
from flockmodel import *
fr = pickle.load(open('flock_frames.pkl', 'rb'))
PV, PF = plane_model()
data = np.memmap(RE + 'full.frames', dtype=np.uint8, mode='r'); FR = 64768
rows = []
for t, f in [(9499, 327), (10219, 677)]:
    cap = np.asarray(data[t * FR:t * FR + 64000]).reshape(200, 320) < 64
    m = mask(fr[f], PV, PF)
    im = np.zeros((200, 320, 3), np.uint8); im[..., 0] = cap * 255; im[..., 1] = m * 255
    rows.append(im)
Image.fromarray(np.concatenate(rows, 0)).resize((960, 1200), Image.NEAREST).save('vis.png')
