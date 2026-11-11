import sys, numpy as np
from PIL import Image
FR=64768
d=np.memmap('/home/dennis/Downloads/acme_303/paper/re/full.frames',dtype=np.uint8,mode='r')
ts=[int(x) for x in sys.argv[2:]]
ims=[]
for t in ts:
    f=np.asarray(d[t*FR:(t+1)*FR]); pal=(f[64000:].reshape(256,3).astype(np.uint16)*255//63).astype(np.uint8)
    ims.append(Image.fromarray(pal[f[:64000]].reshape(200,320,3)).resize((640,400),Image.NEAREST))
W=Image.new('RGB',(1280,400*((len(ims)+1)//2)))
for i,im in enumerate(ims): W.paste(im,((i%2)*640,(i//2)*400))
W.save(sys.argv[1])
