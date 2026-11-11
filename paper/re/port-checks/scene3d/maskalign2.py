exec(open('maskalign.py').read().split('for N in list')[0])
import sys
for N in range(int(sys.argv[1]),int(sys.argv[2]),int(sys.argv[3])):
    r=rec(N); col=r>=0x40; paper=(r>=0x32)&(r<0x40)
    res=[]
    for d1 in range(-1,5):
        for d2 in range(-1,5):
            m=mask(N-d1,3441-(N-d2))
            fn=(col&~m).sum(); fp=(m&paper).sum()
            res.append((fn+fp,fn,fp,d1,d2))
    res.sort()
    print(N,'colour',col.sum(),' '.join('%d[%d+%d](p%d,t%d)'%b for b in res[:5]))
