import pickle, sys, numpy as np
from PIL import Image
S='/tmp/claude-1000/-home-dennis--config-Claude-scratch-workspaces-e97b6b66-a77a-46ac-a12a-f43ab86d2564-b079650b-97d8-49d5-bd62-940ddce6ee3d-scratch-2026-11-10-9f02fa/0a51be05-fc78-48c4-9845-e78eac5ea96a/scratchpad'
sys.path.insert(0, S+'/re-scene3d')
import model
cap=pickle.load(open(S+'/re-scene3d/c4500.cap0.pkl','rb'))
R=model.Renderer(cap['memA'])
L=[(10,20,150,20),(10,30,150,45),(10,70,150,50),(20,80,30,190),(60,190,40,85),(70,90,160,180),(200,10,300,110),(170,120,310,125),(-20,150,340,140),(250,130,255,230),(180,195,300,160),(5,5,6,7)]
buf=bytearray(b'\x3a'*64000); R.oob=0
for l in L: R.line18b7(buf,*l)
img=open('/home/dennis/Downloads/acme_303/paper/re/paper_img.bin','rb').read()
pal=np.frombuffer(img[0xd00+0x169a3:0xd00+0x169a3+768],np.uint8).reshape(256,3).astype(np.float32)/63
o=pal[np.frombuffer(bytes(buf),np.uint8)].reshape(200,320,3)
r=np.asarray(Image.open(S+'/core3d/lines/shot_04500.png').convert('RGB'),np.float32)/255
paper=pal[0x3a]
do=(paper-o).mean(axis=2); dr=(paper-r).mean(axis=2)
mo=do>0.01; mr=dr>0.05
print('orig darkened px', mo.sum(), 'remake (>0.05)', mr.sum(), 'overlap', (mo&mr).sum(), 'IoU %.2f'%((mo&mr).sum()/(mo|mr).sum()))
print('total darkness orig %.1f remake %.1f'%(do.sum(), dr.sum()))
# per line region stats: centroid offsets
out=np.zeros((200,320,3),np.uint8); out[...,0]=(np.clip(do*3,0,1)*255); out[...,1]=(np.clip(dr*3,0,1)*255)
Image.fromarray(out).resize((960,600),Image.NEAREST).save(S+'/core3d/linecmp.png')
