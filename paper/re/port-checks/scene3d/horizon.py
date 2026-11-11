import numpy as np, math, struct
exec(open('/tmp/claude-1000/-home-dennis--config-Claude-scratch-workspaces-e97b6b66-a77a-46ac-a12a-f43ab86d2564-b079650b-97d8-49d5-bd62-940ddce6ee3d-scratch-2026-11-10-9f02fa/0a51be05-fc78-48c4-9845-e78eac5ea96a/scratchpad/core3d/spline.py').read().split("for p in")[0])
def sinT(k):
    k&=1023
    if k==661: return -51912
    if k==905: return -43712
    return int(round(65536*math.sin(2*math.pi*k/1024)))
def hrow(pitch):
    c=sinT(pitch+256) or 1
    q=abs(sinT(pitch)*224)//abs(c); q= q if (sinT(pitch)*224)*c>=0 else -q
    return 100-q
FR=64000+768
data=np.memmap('/home/dennis/Downloads/acme_303/paper/re/full.frames',dtype=np.uint8,mode='r')
def rec(t): return np.asarray(data[t*FR:t*FR+64000]).reshape(200,320)
res={o:0 for o in range(-4,2)}; n=0
for N in range(3445,5470,3):
    r=rec(N)
    # horizon: last sky row in column 0 and 319 (sky index >= 0x80), first ground row (0x60..0x7f)
    hs=[]
    for col in (2,317):
        c=r[:,col]
        for y in range(1,200):
            if c[y-1]>=0x80 and 0x60<=c[y]<0x80: hs.append(y); break
    if len(hs)<2 or hs[0]!=hs[1]: continue
    h=hs[0]; n+=1
    for o in res:
        p=(240*(N+o-3187))>>7
        if hrow(ev(p)[1])==h or hrow(ev(p)[1])==h-1 or hrow(ev(p)[1])==h-2: res[o]+= (hrow(ev(p)[1])==h)
print(n,res)
