# usage: multiscore.py DIR1,DIR2,... ticks...  -> score per dir at the same ticks
import sys, os, numpy as np
from PIL import Image
FR=64000+768
data=np.memmap('/home/dennis/Downloads/acme_303/paper/re/full.frames',dtype=np.uint8,mode='r')
crop=None
if os.environ.get('CROP'): crop=tuple(int(v) for v in os.environ['CROP'].split(','))
def orig(t):
    f=np.asarray(data[t*FR:(t+1)*FR]); pal=(f[64000:].reshape(256,3).astype(np.uint16)*255//63).astype(np.uint8)
    return pal[f[:64000]].reshape(200,320,3).astype(np.float32)
def shot(d,t): return np.asarray(Image.open(os.path.join(d,'shot_%05d.png'%t)).convert('RGB').resize((320,200),Image.BOX),dtype=np.float32)
dirs=sys.argv[1].split(','); ticks=[int(x) for x in sys.argv[2:]]
tot=np.zeros(len(dirs))
for t in ticks:
    o=orig(t)
    sc=[]
    for d in dirs:
        a=np.abs(shot(d,t)-o)
        if crop: x,y,w,h=crop; a=a[y:y+h,x:x+w]
        sc.append(a.mean())
    tot+=sc
    print(t,' '.join('%s:%.2f'%(os.path.basename(d),s) for d,s in zip(dirs,sc)))
print('total',' '.join('%s:%.2f'%(os.path.basename(d),s/len(ticks)) for d,s in zip(dirs,tot)))
