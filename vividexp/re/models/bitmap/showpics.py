# Contact sheet of the bitmap effects' textures (256x256, own palette) and the palette strips
import numpy as np, os
from PIL import Image, ImageDraw
F1='unpacked/files/1/pics/'; F2='unpacked/files/2/pics/'
names=[F1+'plo2.raw',F1+'plo5.raw',F1+'plo6.raw',F2+'plo3.raw',F1+'2x2_3.raw',F1+'2x2_4.raw',F1+'2x2_1.raw',F1+'2x2_2.raw']
sh=Image.new('RGB',(4*264,2*(256+40)),(40,40,40)); d=ImageDraw.Draw(sh)
for i,n in enumerate(names):
    b=open(n,'rb').read(); pal=np.frombuffer(b[32:800],np.uint8).reshape(256,3); px=np.frombuffer(b[800:],np.uint8).reshape(256,256)
    x=(i%4)*264; y=(i//4)*296
    sh.paste(Image.fromarray(pal[px]),(x,y+12)); d.text((x+2,y),os.path.basename(n),fill=(255,255,0))
    strip=np.repeat(pal[None,:128],12,0); sh.paste(Image.fromarray(strip).resize((256,12)),(x,y+272))
sh.save('models/bitmap/runs/pics.png')
