# per-pixel comparison of remake shots (any 4:3 size, box-downsampled to 320x200) with full.frames
import sys, numpy as np
from PIL import Image
FR=64000+768
data=np.memmap('/home/dennis/Downloads/acme_303/paper/re/full.frames',dtype=np.uint8,mode='r')
d=sys.argv[1]; ticks=[int(t) for t in sys.argv[2:]]
for t in ticks:
    f=np.asarray(data[t*FR:(t+1)*FR]); pal=(f[64000:].reshape(256,3).astype(np.float32)*255/63)
    o=pal[f[:64000]].reshape(200,320,3)
    r=np.asarray(Image.open('%s/shot_%05d.png'%(d,t)).convert('RGB').resize((320,200),Image.BOX),np.float32)
    diff=np.abs(r-o).mean(axis=2)
    # mask: exclude the banner handwriting area? report full and the left 60%
    print('tick %d mean %.2f  median %.2f  >24: %.1f%%  >48: %.1f%%' % (t, diff.mean(), np.median(diff), (diff>24).mean()*100, (diff>48).mean()*100))
    Image.fromarray(np.clip(diff*4,0,255).astype(np.uint8)).resize((640,400),Image.NEAREST).save('%s/diff_%05d.png'%(d,t))
