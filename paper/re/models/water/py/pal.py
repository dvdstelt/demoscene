import sys, struct, pickle, numpy as np
O='/tmp/claude-1000/-home-dennis--config-Claude-scratch-workspaces-e97b6b66-a77a-46ac-a12a-f43ab86d2564-b079650b-97d8-49d5-bd62-940ddce6ee3d-scratch-2026-11-10-9f02fa/0a51be05-fc78-48c4-9845-e78eac5ea96a/scratchpad/re-water/'
SIN=pickle.load(open(O+'sin.pkl','rb'))
def pal_buf(ang, shift=11):
    s=SIN[ang&0x3ff]>>shift; c=SIN[(ang+0x100)&0x3ff]>>shift
    R=(0x80-s)&255
    half=s>>1
    G=(0x80+half)&255; B=G
    c2=(c*0xde)>>8
    G=(G+c2)&255; B=(B-c2)&255
    out=[]
    for ch in range(128):
        out.append(((R*ch)>>9,(G*ch)>>9,(B*ch)>>9))
    for ch in range(128,0,-1):
        bl=((0x80-ch)&255)>>1
        out.append((((R*ch)>>9)+bl,((G*ch)>>9)+bl,((B*ch)>>9)+bl))
    return np.array(out,np.int64), (R,G,B)
def upload(buf, level):
    L=level>>8
    if L<=0x80: return (buf*L)>>7
    L=min(L,255); bl=L-128; bh=127-bl
    return (((buf*bh)>>6)+bl)>>1
W=pickle.load(open(O+'x.wrec','rb'))
T=[t for t in W['ticks'] if t[0]>=7744]
FR=64768
fr=np.memmap(O+'x.frames',np.uint8,'r')
t0=7000
bad=0
incs=[]
prev=None
for (tick,palang,sub,lang,cnt,level,slots,nxt,state) in T:
    buf,rgb=pal_buf(palang-1 if palang>2372 else 2372)
    p=upload(buf,level)
    cap=np.array(fr[(tick-t0)*FR+64000:(tick-t0+1)*FR]).reshape(256,3)
    if (p!=cap).any(): bad+=1
    if prev is not None: incs.append(palang-prev)
    prev=palang
print('ticks', len(T), T[0][0], T[-1][0], 'palette mismatches', bad)
print('palang start/end', T[0][1], T[-1][1], 'lang', T[0][3], T[-1][3])
# increment pattern vs sub
import collections
print(collections.Counter((T[i][2], T[i][1]-T[i-1][1]) for i in range(1,len(T))))
print('palang inc total', T[-1][1]-T[0][1], 'over', len(T)-1, 'ticks')
print('sub sequence first 40', [t[2] for t in T[:40]])
print('incs first 40', incs[:40])
# base colour at a few ticks
for tick in (7744, 7900, 8000, 8200, 8400, 8600, 8800):
    t=[x for x in T if x[0]==tick][0]
    print(tick, 'palang', t[1], 'ang&3ff', t[1]&0x3ff, 'RGB', pal_buf(t[1])[1], 'level', t[5], 'counter', t[4])
# level timeline
lv=[(t[0],t[5]) for t in T]
print('first full level tick', [x for x in lv if x[1]==0x8000][0], 'fade-out start', [x for x in lv if x[0]>8500 and x[1]<0x8000][0], 'zero', [x for x in lv if x[0]>8500 and x[1]==0][:1])
print('counter reach 1024 at', [t[0] for t in T if t[4]>=1024][0])
