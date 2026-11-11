import pickle, struct, unicorn
SN='/tmp/claude-1000/-home-dennis--config-Claude-scratch-workspaces-e97b6b66-a77a-46ac-a12a-f43ab86d2564-b079650b-97d8-49d5-bd62-940ddce6ee3d-scratch-2026-11-10-9f02fa/0a51be05-fc78-48c4-9845-e78eac5ea96a/scratchpad/snaps/t8000.snap'
O='/tmp/claude-1000/-home-dennis--config-Claude-scratch-workspaces-e97b6b66-a77a-46ac-a12a-f43ab86d2564-b079650b-97d8-49d5-bd62-940ddce6ee3d-scratch-2026-11-10-9f02fa/0a51be05-fc78-48c4-9845-e78eac5ea96a/scratchpad/re-water/'
s=pickle.load(open(SN,'rb'))
m=s['mem']
SIN=list(struct.unpack_from('<1024i',m,0x22479))
pickle.dump(SIN,open(O+'sin.pkl','wb'))
import math
bad=[i for i in range(1024) if SIN[i]!=round(65536*math.sin(2*math.pi*i/1024))]
print('sin entries differing from round(65536 sin):', len(bad), bad[:10], [SIN[i] for i in bad[:5]], [65536*math.sin(2*math.pi*i/1024) for i in bad[:5]])
print(SIN[:4], SIN[256], SIN[512], SIN[768])
