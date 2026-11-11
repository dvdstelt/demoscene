# usage: offscore.py SHOTDIR offsets ticks...   (shots at T+d), prints mean abs diff vs record T per offset
import sys, os, numpy as np
from PIL import Image
FR=64000+768
data=np.memmap('/home/dennis/Downloads/acme_303/paper/re/full.frames',dtype=np.uint8,mode='r')
def orig(t):
    f=np.asarray(data[t*FR:(t+1)*FR]); pal=(f[64000:].reshape(256,3).astype(np.uint16)*255//63).astype(np.uint8)
    return pal[f[:64000]].reshape(200,320,3).astype(np.float32)
def shot(d,t):
    n=os.path.join(d,'shot_%05d.png'%int(t)) if t==int(t) else os.path.join(d,'shot_%08.2f.png'%t)
    return np.asarray(Image.open(n).convert('RGB').resize((320,200),Image.BOX),dtype=np.float32)
d=sys.argv[1]; offs=[float(x) for x in sys.argv[2].split(',')]; ticks=[int(x) for x in sys.argv[3:]]
tot=np.zeros(len(offs))
for t in ticks:
    o=orig(t); sc=[np.abs(shot(d,t+x)-o).mean() for x in offs]; tot+=sc
    print(t,' '.join('%+.1f:%.2f'%(x,s) for x,s in zip(offs,sc)), 'best %+.1f'%offs[int(np.argmin(sc))])
print('total',' '.join('%+.1f:%.2f'%(x,s/len(ticks)) for x,s in zip(offs,tot)))
