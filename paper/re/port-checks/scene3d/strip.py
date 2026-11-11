import sys,os
from PIL import Image, ImageDraw
d=sys.argv[1]; out=sys.argv[2]; names=sys.argv[3:]
ims=[Image.open(os.path.join(d,n)).convert('RGB').resize((320,240)) for n in names]
cols=4; rows=(len(ims)+cols-1)//cols
sheet=Image.new('RGB',(cols*322,rows*242),(255,0,255))
for i,(im,n) in enumerate(zip(ims,names)):
    x=(i%cols)*322; y=(i//cols)*242; sheet.paste(im,(x,y)); ImageDraw.Draw(sheet).text((x+4,y+4),n,fill=(255,255,0))
sheet.save(out,quality=90)
