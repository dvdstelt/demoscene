import pickle, struct, numpy as np
S='/tmp/claude-1000/-home-dennis--config-Claude-scratch-workspaces-e97b6b66-a77a-46ac-a12a-f43ab86d2564-b079650b-97d8-49d5-bd62-940ddce6ee3d-scratch-2026-11-10-9f02fa/0a51be05-fc78-48c4-9845-e78eac5ea96a/scratchpad'
FR=64000+768
data=np.memmap('/home/dennis/Downloads/acme_303/paper/re/full.frames',dtype=np.uint8,mode='r')
for name in ['c3300','c4500','d4500','c5500']:
    cap=pickle.load(open(S+'/re-scene3d/%s.cap0.pkl'%name,'rb'))
    mB=cap['memB']; back=struct.unpack_from('<I',mB,0x1edf9)[0]
    img=np.frombuffer(mB[back:back+64000],np.uint8)
    t=cap['tick']
    best=None
    for tt in range(t-3,t+4):
        f=np.asarray(data[tt*FR:tt*FR+64000])
        d=(f!=img).sum()
        print(name,'vs frame',tt,'diff',d)
