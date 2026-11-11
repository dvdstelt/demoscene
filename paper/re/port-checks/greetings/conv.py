import sys, pickle, numpy as np
R='/tmp/claude-1000/-home-dennis--config-Claude-scratch-workspaces-e97b6b66-a77a-46ac-a12a-f43ab86d2564-b079650b-97d8-49d5-bd62-940ddce6ee3d-scratch-2026-11-10-9f02fa/0a51be05-fc78-48c4-9845-e78eac5ea96a/scratchpad/re-water/'
SIN=pickle.load(open(R+'sin.pkl','rb'))
W=pickle.load(open(R+'x.wrec','rb'))
tex=np.load(R+'gen_tex.npy').reshape(-1)[:65536].reshape(256,256).astype(float)
def i32(v): v&=0xffffffff; return v-(1<<32) if v&0x80000000 else v
for fr in (170,300,450):
    d=W['dumps'][fr]
    wv=np.frombuffer(d['waves'],np.int32).reshape(72,3).astype(np.int64)
    hw=np.frombuffer(d['hwarp'],np.uint8).reshape(203,320)[1:201].astype(float)
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
    X=sum(ly[j]*lx[i]*P[j,i,0] for j in range(3) for i in range(3))
    Y=sum(ly[j]*lx[i]*P[j,i,1] for j in range(3) for i in range(3))
    col=X/4; row=Y/4
    def bil(c,rw):
        c0=np.floor(c); r0=np.floor(rw); fc=c-c0; fr_=rw-r0
        c0=c0.astype(int); r0=r0.astype(int)
        g=lambda a,b: tex[(b)&255,(a)&255]
        return (g(c0,r0)*(1-fc)+g(c0+1,r0)*fc)*(1-fr_)+(g(c0,r0+1)*(1-fc)+g(c0+1,r0+1)*fc)*fr_
    near=np.floor(tex[np.floor(row).astype(int)&255, np.floor(col).astype(int)&255]*20/32)
    out=['frame %d tick %d: nearest-per-pixel vs orig mean|d| %.2f'%(fr,W['frames'][fr]['tick'],np.abs(near-hw).mean())]
    for off in (-0.5,0,0.5):
        b=bil(col-0.5+off,row-0.5+off)*20/32
        out.append('bilinear off %+.1f: mean|d| %.2f mean d %.2f'%(off,np.abs(b-hw).mean(),(b-hw).mean()))
    print('; '.join(out))
