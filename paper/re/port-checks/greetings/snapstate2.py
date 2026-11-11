import sys, pickle, numpy as np, struct
R='/tmp/claude-1000/-home-dennis--config-Claude-scratch-workspaces-e97b6b66-a77a-46ac-a12a-f43ab86d2564-b079650b-97d8-49d5-bd62-940ddce6ee3d-scratch-2026-11-10-9f02fa/0a51be05-fc78-48c4-9845-e78eac5ea96a/scratchpad/re-water/'
sys.path.insert(0, R+'py')
import model
SIN=pickle.load(open(R+'sin.pkl','rb'))
grid=np.load(R+'mem8000_grid.npy').astype(np.int64)
tex=np.load(R+'gen_tex.npy'); sh=np.load(R+'mem8000_shade.npy')
print(sh.shape, sh[1,20,:8], sh[0,20,:8])
T2=sh[0]
Wm=model.warp(grid,tex,T2)
hm=np.load(R+'mem8000_hmap.npy'); vga=np.load(R+'mem8000_vga.npy')
d=(Wm!=hm[:200])
print('warp vs hmap differing', d.sum(), 'rows with diffs', np.unique(np.nonzero(d)[0])[:20], '...')
lm=np.load(R+'mem8000_lightmap.npy')
H=np.zeros((203,320),np.int64); H[1:203]=hm[:202]
best=[]
for a in range(250,400):
    B=model.bump(H,lm,a,SIN)
    rows=(B!=vga).sum(1)
    best.append((int((B[1:199]!=vga[1:199]).sum()),a, np.nonzero(rows==0)[0][:3]))
best.sort(); print(best[:3])
B=model.bump(H,lm,342,SIN)
rows=(B!=vga).sum(1); print('diff rows', np.nonzero(rows)[0])
# text: which slots
m=None
