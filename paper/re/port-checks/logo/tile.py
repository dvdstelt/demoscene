# tile.py out x y w h scale shot1 shot2 ... : native crops (VGA rect) side by side, upscaled by `scale` (nearest)
import sys
from PIL import Image
o=sys.argv[1]; x,y,w,h,sc=map(float,sys.argv[2:7]); shots=sys.argv[7:]
crops=[]
for s in shots:
    im=Image.open(s).convert('RGB'); sx=im.width/320; sy=im.height/200
    c=im.crop((int(x*sx),int(y*sy),int((x+w)*sx),int((y+h)*sy)))
    crops.append(c.resize((int(c.width*sc),int(c.height*sc)),Image.NEAREST))
W=sum(c.width for c in crops)+6*(len(crops)-1)
out=Image.new('RGB',(W,crops[0].height),(255,0,255)); px=0
for c in crops: out.paste(c,(px,0)); px+=c.width+6
out.save(o)
