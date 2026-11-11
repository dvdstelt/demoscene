import numpy as np
from PIL import Image
SEG=open('/home/dennis/Downloads/acme_303/paper/re/paper_img.bin','rb').read()[0xd00:]
def nib(a,rows):
    b=np.frombuffer(SEG,np.uint8,rows*112,a).reshape(rows,112)
    o=np.empty((rows,224),np.uint8); o[:,0::2]=b>>4; o[:,1::2]=b&15; return o
sk=nib(0x1bf03,83); fm=nib(0x196c3,92)
np.save('sketch.npy',sk); np.save('fill.npy',fm)
print(np.unique(sk,return_counts=True))
# crop of sketch, nearest x8
c=sk[0:45,0:70]
Image.fromarray((255-c*20).astype(np.uint8)).resize((70*10,45*10),Image.NEAREST).save('sk_zoom.png')
pal=np.frombuffer(SEG,np.uint8,768,0x169a3).reshape(256,3).astype(np.uint16)*255//63
f=pal[0x40+fm].astype(np.uint8); f[fm==0]=(255-np.pad(sk,((0,9),(0,0)))[fm==0]*20)[:,None]
Image.fromarray(f[0:45,0:70]).resize((700,450),Image.NEAREST).save('fm_zoom.png')
