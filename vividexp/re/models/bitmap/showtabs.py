# Contact sheet of all bitmap-effect tables (1x1: 320x200, 2x2: 160x100 upscaled)
import numpy as np, os
from PIL import Image, ImageDraw
F1='unpacked/files/1/tabs/'; F2='unpacked/files/2/tabs/'
names=[F1+f for f in sorted(os.listdir(F1))]+[F2+f for f in sorted(os.listdir(F2))]
W,H=320,200
cols=4; rows=(len(names)+cols-1)//cols
sheet=Image.new('L',(cols*(W+4),rows*(H+14)),0)
d=ImageDraw.Draw(sheet)
for i,n in enumerate(names):
    t=np.frombuffer(open(n,'rb').read(),np.uint8)
    if len(t)==16000: im=Image.fromarray(t.reshape(100,160)).resize((320,200),Image.NEAREST)
    else: im=Image.fromarray(t.reshape(200,320))
    x=(i%cols)*(W+4); y=(i//cols)*(H+14)
    sheet.paste(im,(x,y+14)); d.text((x+2,y+1),os.path.basename(n),fill=255)
sheet.save('models/bitmap/runs/tabs.png')
