exec(open('maskalign.py').read().split('for N in list')[0])
import sys
tot={}
for N in range(int(sys.argv[1]),int(sys.argv[2]),int(sys.argv[3])):
    r=rec(N); col=r>=0x40
    cands=[(d1,d2) for d1 in range(-1,5) for d2 in range(-1,5)]
    ms={c:mask(N-c[0],3441-(N-c[1])) for c in cands}
    allc=np.all([ms[c] for c in cands],axis=0); nonec=~np.any([ms[c] for c in cands],axis=0)
    dis=~(allc|nonec)
    if dis.sum()==0: continue
    res=sorted([((ms[c]==col)[dis].mean(),c) for c in cands],reverse=True)
    for sc,c in res: tot[c]=tot.get(c,0)+sc
    print(N,'dis',dis.sum(),' '.join('%.3f%s'%(s,c) for s,c in res[:5]))
print(sorted([(v,k) for k,v in tot.items()],reverse=True)[:8])
