import sys, pickle, numpy as np
R='/tmp/claude-1000/-home-dennis--config-Claude-scratch-workspaces-e97b6b66-a77a-46ac-a12a-f43ab86d2564-b079650b-97d8-49d5-bd62-940ddce6ee3d-scratch-2026-11-10-9f02fa/0a51be05-fc78-48c4-9845-e78eac5ea96a/scratchpad/re-water/'
D='/tmp/claude-1000/-home-dennis--config-Claude-scratch-workspaces-e97b6b66-a77a-46ac-a12a-f43ab86d2564-b079650b-97d8-49d5-bd62-940ddce6ee3d-scratch-2026-11-10-9f02fa/0a51be05-fc78-48c4-9845-e78eac5ea96a/scratchpad/part-greetings/dump/'
SIN=pickle.load(open(R+'sin.pkl','rb'))
W=pickle.load(open(R+'x.wrec','rb'))
tex=np.load(R+'gen_tex.npy').reshape(-1)[:65536].reshape(256,256).astype(float)
def i32(v): v&=0xffffffff; return v-(1<<32) if v&0x80000000 else v
w0=np.frombuffer(W['dumps'][0]['waves'],np.int32).reshape(72,3).astype(np.int64)
wv=w0.copy(); wv[:,0]=w0[:,0]+w0[:,2]*254
P=np.zeros((3,3,2)); r=0
for j in range(3):
    for i in range(3):
        xs=ys=0
        for cl in (4,3,2,1):
            for c in range(2):
                ph,amp,_=wv[r]; idx=(i32(int(ph)<<cl)>>7)&0x3ff; s=i32(SIN[idx]*int(amp))>>16
                if c==0: xs+=s>>cl
                else: ys+=s>>cl
                r+=1
        P[j,i]=(xs,ys)
y,x=np.mgrid[0:200,0:320].astype(float)
def L(u): return [(u-1)*(u-2)/2, u*(2-u), u*(u-1)/2]
lx=L(3*x/512); ly=L(5*y/512)
X=sum(ly[j]*lx[i]*P[j,i,0] for j in range(3) for i in range(3)); Y=sum(ly[j]*lx[i]*P[j,i,1] for j in range(3) for i in range(3))
c=X/4-0.5; rw=Y/4-0.5
def cr(f):
    return np.stack([f*(-0.5+f*(1-0.5*f)), 1+f*f*(-2.5+1.5*f), f*(0.5+f*(2-1.5*f)), f*f*(-0.5+0.5*f)])
c0=np.floor(c); r0=np.floor(rw); wc=cr(c-c0); wr=cr(rw-r0); c0=c0.astype(int); r0=r0.astype(int)
b=np.zeros_like(c)
for a in range(4):
    for bb in range(4):
        b+=wr[a]*wc[bb]*tex[(r0-1+a)&255,(c0-1+bb)&255]
b*=20/32
H=np.load(R+'mem8000_hmap.npy').astype(float)[:200]
h=np.fromfile(D+'h_7997.f32',np.float32).reshape(200,320)[::-1].astype(float)
m=np.abs(b-H)<30   # background pixels (no letters)
m&= (H<=159)
print('pixels',m.sum())
print('exact-tex bicubic vs orig %.2f | GPU (hi-res cloud) vs orig %.2f | GPU vs exact-tex bicubic %.2f (mean %.2f)'%(np.abs(b-H)[m].mean(), np.abs(h-H)[m].mean(), np.abs(h-b)[m].mean(), (h-b)[m].mean()))
