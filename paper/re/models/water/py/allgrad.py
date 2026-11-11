import sys, pickle, numpy as np
sys.path.insert(0,'/tmp/claude-1000/-home-dennis--config-Claude-scratch-workspaces-e97b6b66-a77a-46ac-a12a-f43ab86d2564-b079650b-97d8-49d5-bd62-940ddce6ee3d-scratch-2026-11-10-9f02fa/0a51be05-fc78-48c4-9845-e78eac5ea96a/scratchpad/re-water/py')
from model import grid_from_waves, load_sin, O
SIN=load_sin()
W=pickle.load(open(O+'x.wrec','rb'))
tex=np.load(O+'gen_tex.npy').reshape(256,256).astype(np.int64)
py,px=np.mgrid[0:8,0:8]
def warp_np(G):
    G=G.astype(np.int64)
    out=np.zeros((200,320),np.int64)
    v00=G[:-1,:-1]; v10=G[:-1,1:]; v01=G[1:,:-1]; v11=G[1:,1:]
    dx=(v10-v00)>>3; ddx=((v11-v01)-(v10-v00))>>6; dy=(v01-v00)>>3
    # shapes (25,40,3) -> broadcast with (8,8)
    val=(v00[:,:,None,None,:]+py[None,None,:,:,None]*dy[:,:,None,None,:]+px[None,None,:,:,None]*(dx[:,:,None,None,:]+py[None,None,:,:,None]*ddx[:,:,None,None,:]))&0xffff
    a=val[...,0]>>8; b=val[...,1]>>8
    h=(tex[a,b]*20)>>5
    return h.transpose(0,2,1,3).reshape(200,320)
mx=0; my=0; worst=None
for n,f in enumerate(W['frames']):
    wv=np.frombuffer(f['waves'],np.int32).reshape(72,3).astype(np.int64).tolist()
    G=grid_from_waves(wv,SIN)
    h=warp_np(G)
    gx=abs(h[1:-1,:-2]-h[1:-1,2:]).max(); gy=abs(h[:-2,1:-1]-h[2:,1:-1]).max()
    if max(gx,gy)>max(mx,my): worst=(n,f['tick'],gx,gy)
    mx=max(mx,gx); my=max(my,gy)
    if n==170:
        d=W['dumps'][170]; hw=np.frombuffer(d['hwarp'],np.uint8).reshape(203,320)[1:201]
        print('check frame 170 (waves at frame start vs dump):', (h!=hw).sum())
print('all frames: max |dx|',mx,'max |dy|',my,'worst',worst)
