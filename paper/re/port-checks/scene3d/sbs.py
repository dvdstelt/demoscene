# sbs.py OUT tick crop(x,y,w,h) dir1 dir2 ... : original crop + crops from dirs side by side
import sys, os, numpy as np
from PIL import Image
FR=64000+768
data=np.memmap('/home/dennis/Downloads/acme_303/paper/re/full.frames',dtype=np.uint8,mode='r')
out=sys.argv[1]; rows=[]
specs=sys.argv[2:]
# groups separated by '--': tick crop dirs...
groups=[]; cur=[]
for a in specs:
    if a=='--': groups.append(cur); cur=[]
    else: cur.append(a)
if cur: groups.append(cur)
PW=int(os.environ.get('PW','360'))
for g in groups:
    t=int(g[0]); x,y,w,h=map(float,g[1].split(',')); dirs=g[2:]
    f=np.asarray(data[t*FR:(t+1)*FR]); pal=(f[64000:].reshape(256,3).astype(np.uint16)*255//63).astype(np.uint8)
    o=Image.fromarray(pal[f[:64000]].reshape(200,320,3))
    ph=int(PW*h/w*0.75*320/200* (200/320) / 0.75 ) if False else int(PW*(h*1.2)/w)
    ims=[o.resize((PW,ph),Image.NEAREST,box=(x,y,x+w,y+h))]
    for d in dirs:
        im=Image.open(os.path.join(d,'shot_%05d.png'%t)).convert('RGB'); sx=im.width/320; sy=im.height/200
        ims.append(im.resize((PW,ph),Image.LANCZOS,box=(x*sx,y*sy,(x+w)*sx,(y+h)*sy)))
    row=Image.new('RGB',(PW*len(ims)+4*(len(ims)-1),ph),(255,0,255))
    for i,im in enumerate(ims): row.paste(im,(i*(PW+4),0))
    rows.append(row)
W=max(r.width for r in rows); H=sum(r.height+4 for r in rows)
sheet=Image.new('RGB',(W,H),(255,0,255)); yy=0
for r in rows: sheet.paste(r,(0,yy)); yy+=r.height+4
sheet.save(out,quality=92); print('wrote',out)
