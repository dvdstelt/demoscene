import numpy as np, sys
from PIL import Image
from mm import M
s = M(1250)
pal = (np.frombuffer(s.b[0x169a3:0x169a3+768], np.uint8).reshape(256,3).astype(int)*255//63).astype(np.uint8)
a = np.load(sys.argv[1]); b = np.load(sys.argv[2])
d = np.zeros((200,320,3), np.uint8); d[a != b] = (255,0,0)
im = np.concatenate([pal[a], pal[b], d], 0)
Image.fromarray(im).resize((640, 1200), Image.NEAREST).save(sys.argv[3])
