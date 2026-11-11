import pickle, numpy as np, struct
O='/tmp/claude-1000/-home-dennis--config-Claude-scratch-workspaces-e97b6b66-a77a-46ac-a12a-f43ab86d2564-b079650b-97d8-49d5-bd62-940ddce6ee3d-scratch-2026-11-10-9f02fa/0a51be05-fc78-48c4-9845-e78eac5ea96a/scratchpad/re-water/'
W=pickle.load(open(O+'x.wrec','rb'))
for n in sorted(W['dumps']):
    d=W['dumps'][n]; f=W['frames'][n]
    print(n, {k:v for k,v in f.items() if not isinstance(v,(bytes,bytearray))}, {k:(len(v) if isinstance(v,(bytes,bytearray)) else v) for k,v in d.items()})
print(W['ticks'][:3])
print(len(W['ticks']))
