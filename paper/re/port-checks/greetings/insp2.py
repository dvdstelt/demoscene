import pickle, numpy as np, struct
O='/tmp/claude-1000/-home-dennis--config-Claude-scratch-workspaces-e97b6b66-a77a-46ac-a12a-f43ab86d2564-b079650b-97d8-49d5-bd62-940ddce6ee3d-scratch-2026-11-10-9f02fa/0a51be05-fc78-48c4-9845-e78eac5ea96a/scratchpad/re-water/'
W=pickle.load(open(O+'x.wrec','rb'))
for t in W['ticks']:
    if 7740<=t[0]<=7752 or 8810<=t[0]<=8820:
        sl=t[6]; lv=[sl[4+5*s] for s in range(4)]
        print(t[0], 'palang',t[1],'sub',t[2],'lang',t[3],'cnt',t[4],'level',t[5],'lv',lv,'state',t[8])
w0=np.frombuffer(W['waves0'],np.int32).reshape(72,3) if isinstance(W['waves0'],(bytes,bytearray)) else W['waves0']
print(np.array(w0)[:3])
d=W['dumps'][0]; print(np.frombuffer(d['waves'],np.int32).reshape(72,3)[:3])
d=W['dumps'][1]; print(np.frombuffer(d['waves'],np.int32).reshape(72,3)[:3])
