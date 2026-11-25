# Contour view of tables: python3 models/bitmap/viewtab.py tab4.2x2 tab5.2x2 -> runs/view.png
import numpy as np, sys
from PIL import Image
sys.path.insert(0,'models/bitmap')
from tabfit import *
ims=[]
for n in sys.argv[1:]:
    t=tab(n)
    # contour bands every 16
    c=((t%16)<2)*255
    rgb=np.stack([t,t,np.maximum(t//2,c)],-1).astype(np.uint8)
    im=Image.fromarray(rgb)
    s=4 if t.shape[0]==100 else 2
    ims.append(im.resize((t.shape[1]*s,t.shape[0]*s),Image.NEAREST))
W=sum(i.width for i in ims)+10*len(ims); H=max(i.height for i in ims)
sh=Image.new('RGB',(W,H)); X=0
for i in ims: sh.paste(i,(X,0)); X+=i.width+10
sh.save('models/bitmap/runs/view.png')
