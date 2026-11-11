# Static page check: remake (box-downscaled to 320x200) vs capture record, both smoothed with a 5x5 box to remove
# the grain realisation's quantisation; reports mean abs difference per region (0..255).
import sys, numpy as np
from PIL import Image
FR=64768
d=np.memmap('/home/dennis/Downloads/acme_303/paper/re/full.frames',dtype=np.uint8,mode='r')
def cap(t):
    f=np.asarray(d[t*FR:(t+1)*FR]); pal=(f[64000:].reshape(256,3).astype(np.float32)*255/63)
    return pal[f[:64000]].reshape(200,320,3)
def box(a,k=5):
    p=k//2; a=np.pad(a,((p,p),(p,p),(0,0)),mode='edge'); o=np.zeros_like(a[p:-p,p:-p])
    for dy in range(k):
        for dx in range(k): o+=a[dy:dy+200,dx:dx+320]
    return o/k/k
shot,rec=sys.argv[1],int(sys.argv[2])
r=np.asarray(Image.open(shot).convert('RGB').resize((320,200),Image.BOX),dtype=np.float32)
o=cap(rec)
regions={'all':(0,0,320,200),'paper top':(0,0,320,9),'present the':(10,8,310,44),'logo':(44,48,232,96),'intro':(170,150,145,48),'paper bottom-left':(0,150,160,50)}
for n,(x,y,w,h) in regions.items():
    a=box(r)[y:y+h,x:x+w]; b=box(o)[y:y+h,x:x+w]
    print('%-18s smoothed mad %.2f  mean remake %.1f orig %.1f   raw mad %.2f'%(n,np.abs(a-b).mean(),a.mean(),b.mean(),np.abs(r[y:y+h,x:x+w]-o[y:y+h,x:x+w]).mean()))
