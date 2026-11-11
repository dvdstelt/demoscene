import sys
import numpy as np
from PIL import Image
pal = np.frombuffer(open('/home/dennis/Downloads/acme_303/paper/re/paper_img.bin','rb').read()[0xd00+0x169a3:0xd00+0x169a3+768], np.uint8).reshape(256,3).astype(np.uint16)*255//63
ims = [pal[np.frombuffer(open(f,'rb').read()[:64000], np.uint8)].reshape(200,320,3).astype(np.uint8) for f in sys.argv[2:]]
Image.fromarray(np.concatenate(ims, axis=1)).resize((320*len(ims)*2, 400), Image.NEAREST).save(sys.argv[1])
