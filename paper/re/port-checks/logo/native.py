# native-resolution crop of a shot: native.py out.png shot.png x y w h (VGA coords)
import sys
from PIL import Image
o,s=sys.argv[1],sys.argv[2]; x,y,w,h=map(float,sys.argv[3:7])
im=Image.open(s).convert('RGB'); sx=im.width/320; sy=im.height/200
im.crop((int(x*sx),int(y*sy),int((x+w)*sx),int((y+h)*sy))).save(o)
