import pickle, numpy as np
O='/tmp/claude-1000/-home-dennis--config-Claude-scratch-workspaces-e97b6b66-a77a-46ac-a12a-f43ab86d2564-b079650b-97d8-49d5-bd62-940ddce6ee3d-scratch-2026-11-10-9f02fa/0a51be05-fc78-48c4-9845-e78eac5ea96a/scratchpad/re-water/'
W=pickle.load(open(O+'x.wrec','rb'))
for n in (170,300,450,600):
    d=W['dumps'][n]
    for key in ('hwarp','htext'):
        h=np.frombuffer(d[key],np.uint8).reshape(203,320).astype(int)[1:201]
        dx=h[1:-1,:-2]-h[1:-1,2:]
        dy=h[:-2,1:-1]-h[2:,1:-1]
        print(n,key,'dx out of range',((dx>127)|(dx<-128)).sum(),'dy out',((dy>127)|(dy<-128)).sum(),'max|dx|',abs(dx).max(),'max|dy|',abs(dy).max(), 'p99 |dx|', np.percentile(abs(dx),99))
# glyph max gradient
g=np.load(O+'mem8000_glyph.npy').astype(int)
gx=g[:,:-2]-g[:,2:]; gy=g[:-2]-g[2:]
print('glyph max |dx|',abs(gx).max(),'max |dy|',abs(gy).max(), 'count>127', (abs(gx)>127).sum(), (abs(gy)>127).sum())
