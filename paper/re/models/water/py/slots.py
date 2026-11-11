import sys, struct, pickle, numpy as np
sys.path.insert(0,'/home/dennis/Downloads/acme_303/paper/re')
import rep
O='/tmp/claude-1000/-home-dennis--config-Claude-scratch-workspaces-e97b6b66-a77a-46ac-a12a-f43ab86d2564-b079650b-97d8-49d5-bd62-940ddce6ee3d-scratch-2026-11-10-9f02fa/0a51be05-fc78-48c4-9845-e78eac5ea96a/scratchpad/re-water/'
W=pickle.load(open(O+'x.wrec','rb'))
T=[t for t in W['ticks'] if t[0]>=7744]
prev=None
for t in T:
    sl=t[6]
    cur=[(sl[4+5*s], struct.unpack_from('<I',sl,5+5*s)[0]) for s in range(4)]
    if prev is not None:
        for s in range(4):
            if cur[s][1]!=prev[s][1]:
                print('tick',t[0],'n',t[0]-7743,'slot',s,'loads',repr(rep.cstr(cur[s][1])),'level',cur[s][0])
            if cur[s][0]==32 and prev[s][0]==31: print('   tick',t[0],'slot',s,'full')
    prev=cur
# model
def model(N):
    slots=[[0,None] for _ in range(4)]; idx=0; state=0
    names=[' ',' ',' ',' ','hello','     jamm','halcyon','   orange','   bullfrog','subliminal','     acme','valhalla','oxygene','    cubic','amen','     pulse']
    k=0; ev=[]
    for n in range(1,N+1):
        s=slots[idx]
        if state:
            s[0]+=1
            if s[0]==32: state=0; idx=(idx+1)&3
        elif s[0]==0:
            state=1; s[1]=names[k]; ev.append((n,idx,names[k])); k=(k+1)%len(names)
        else: s[0]-=1
    return ev
ev=model(1074)
print([(n+7743,s,nm.strip()) for n,s,nm in ev])
