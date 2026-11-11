import sys, numpy as np
from PIL import Image
FR=64768
d=np.memmap(sys.argv[1],dtype=np.uint8,mode='r')
t=int(sys.argv[2]); x0,y0,w,h,z=map(int,sys.argv[4:9])
f=d[t*FR:(t+1)*FR]
idx=f[:64000].reshape(200,320)
pal=f[64000:].reshape(256,3).astype(np.uint16)
gain = float(sys.argv[9]) if len(sys.argv)>9 else 1
rgb=np.clip(pal[idx]*255//63*gain,0,255).astype(np.uint8)
im=Image.fromarray(rgb[y0:y0+h,x0:x0+w]).resize((w*z,h*z),Image.NEAREST)
im.save(sys.argv[3])
print(idx[y0:y0+h, x0:x0+w][:12,:24])
