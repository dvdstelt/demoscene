import numpy as np, math, sys
FR=64000+768
data=np.memmap('/home/dennis/Downloads/acme_303/paper/re/full.frames',dtype=np.uint8,mode='r')
def rec(t): return np.asarray(data[t*FR:t*FR+64000]).reshape(200,320)
def sinT(k):
    k&=1023
    if k==661: return -51912
    if k==905: return -43712
    return int(round(65536*math.sin(2*math.pi*k/1024)))
SIN=np.array([sinT(k) for k in range(1024)],dtype=np.int64)
ph0=[-1975454324,-660127738,-289573399,392987195,-786641111]
sp=[104,34,-84,27,-19]; fq=[-17,-9,-117,178,-462]; am=[146,107,14,90,121]
def table(P):
    x=0x800-np.arange(0x800,dtype=np.int64)
    s=np.zeros(0x800,dtype=np.int64)
    for k in range(5):
        ph=(ph0[k]+sp[k]*(P-3186))
        a=((fq[k]*x+ph)&0xffffffff)>>5
        s=s+SIN[a&0x3ff]*am[k]
    s=((s+2**31)%2**32)-2**31
    v=(s>>18)+0x80
    return (np.clip(v,0,0xfa)>>1).astype(np.int64)
def mask(P,T):
    N=table(P)
    rowv=(N[0x207-np.arange(200)]-0x12)&0xff
    val=(N[None,:320]+rowv[:,None])&0xff
    return val>=T   # colour
for N in list(range(int(sys.argv[1]),int(sys.argv[2]),int(sys.argv[3]))):
    r=rec(N); col=r>=0x40
    best=[]
    for d1 in range(0,4):
        for d2 in range(0,4):
            m=mask(N-d1,3441-(N-d2))
            best.append(((m==col).mean(),d1,d2))
    best.sort(reverse=True)
    print(N, ' '.join('%.4f(p%d,t%d)'%b for b in best[:4]), 'colourfrac %.3f'%col.mean())
