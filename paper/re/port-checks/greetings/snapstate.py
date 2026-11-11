import sys, pickle, numpy as np, struct
R='/tmp/claude-1000/-home-dennis--config-Claude-scratch-workspaces-e97b6b66-a77a-46ac-a12a-f43ab86d2564-b079650b-97d8-49d5-bd62-940ddce6ee3d-scratch-2026-11-10-9f02fa/0a51be05-fc78-48c4-9845-e78eac5ea96a/scratchpad/re-water/'
sys.path.insert(0, R+'py')
import model
SIN=pickle.load(open(R+'sin.pkl','rb'))
W=pickle.load(open(R+'x.wrec','rb'))
w0=np.frombuffer(W['dumps'][0]['waves'],np.int32).reshape(72,3).astype(np.int64)
grid=np.load(R+'mem8000_grid.npy').astype(np.int64)
wv=np.load(R+'mem8000_waves.npy').astype(np.int64)
# how many advances in the snapshot waves
n=((wv[:,0]-w0[:,0])&0xffffffff)
print('advances in snap waves', set(((n*pow(1,1))).tolist()) if False else (n[0], w0[0,2]))
for k in range(200,280):
    ph=(w0[:,0]+w0[:,2]*k)
    ph=((ph+2**31)%2**32)-2**31
    if (ph==wv[:,0]).all(): print('snapshot waves = %d advances -> after IRQ %d'%(k,7743+k))
    W2=np.stack([ph,w0[:,1],w0[:,2]],1)
    G=model.grid_from_waves(W2.tolist(),SIN)
    if (G==grid).all(): print('grid matches %d advances -> state after IRQ %d'%(k,7743+k))
hm=np.load(R+'mem8000_hmap.npy'); vga=np.load(R+'mem8000_vga.npy')
lm=np.load(R+'mem8000_lightmap.npy')
H=np.zeros((203,320),np.int64); H[1:203]=hm[:202]
for a in range(300,360):
    B=model.bump(H,lm,a,SIN)
    d=(B[1:199,1:319]!=vga[1:199,1:319]).sum()
    if d<2000: print('lang',a,'diff',d)
