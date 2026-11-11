# Checks the ink front against the capture: remake at integer tick t (pixel centres) vs capture record t+off.
import sys, numpy as np
from PIL import Image
FR=64768
d=np.memmap('/home/dennis/Downloads/acme_303/paper/re/full.frames',dtype=np.uint8,mode='r')
def cap(t): return np.asarray(d[t*FR:t*FR+64000]).reshape(200,320)
D=sys.argv[1]; ticks=[int(x) for x in sys.argv[2].split(',')]; off=int(sys.argv[3]) if len(sys.argv)>3 else 1
def shot(t):
    im=np.asarray(Image.open('%s/shot_%05d.png'%(D,t)).convert('RGB'),dtype=np.float32)/255
    H,W=im.shape[:2]
    ys=((np.arange(200)+0.5)*H/200).astype(int); xs=((np.arange(320)+0.5)*W/320).astype(int)
    return im[ys][:,xs]
none=shot(5612); full=shot(6017)
dist=np.abs(full-none).sum(2)
ok=dist>0.15
tot=0
for t in ticks:
    r=shot(t)
    rem=(np.abs(r-full).sum(2) < np.abs(r-none).sum(2))
    c=cap(t+off); ci=(c>=0x41)&(c<=0x4f)
    m=ok&(rem!=ci)
    print('tick',t,'capture rec',t+off,'ink(capture)',(ci&ok).sum(),'ink(remake)',(rem&ok).sum(),'mismatch',m.sum(), [ (x,y) for y,x in zip(*np.nonzero(m))][:8])
