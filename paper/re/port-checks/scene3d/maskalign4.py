exec(open('maskalign.py').read().split('for N in list')[0])
import sys
def ncont(P, idx):
    x=0x800-idx
    s=np.zeros_like(idx,dtype=np.float64)
    for k in range(5):
        ph=(ph0[k]+sp[k]*(P-3186))
        s+=am[k]/4.0*np.sin(2*np.pi*((fq[k]*x+ph)%32768)/32768)
    return np.clip(s+128,0,250)/2
def cmask(P,T):
    xs=np.arange(320)+0.0; rs=np.arange(200)+0.0
    v=ncont(P,xs)[None,:]+ncont(P,0x207-rs)[:,None]-18.5
    v=np.where(v< -0.5, v+256, v)
    return v>=T
tot={}
for N in range(int(sys.argv[1]),int(sys.argv[2]),int(sys.argv[3])):
    r=rec(N); col=r>=0x40
    ints=[(d1,d2) for d1 in range(2,6) for d2 in range(1,5)]
    ms={c:mask(N-c[0],3441-(N-c[1])) for c in ints}
    allc=np.all([ms[c] for c in ints],axis=0); nonec=~np.any([ms[c] for c in ints],axis=0)
    dis=~(allc|nonec)
    if dis.sum()<200: continue
    for d in (3,3.25,3.5,3.75,4):
        for e in (0.5,1,1.5):
            m=cmask(N-d,3441-(N-d+e))
            tot[(d,e)]=tot.get((d,e),0)+(m==col)[dis].mean()
    for c in ((3,2),(4,3)):
        tot[c]=tot.get(c,0)+(ms[c]==col)[dis].mean()
print(sorted([(round(v,3),k) for k,v in tot.items()],reverse=True))
