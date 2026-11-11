import struct
img=open('/home/dennis/Downloads/acme_303/paper/re/paper_img.bin','rb').read()
C=0xd00
def i32(a): return struct.unpack_from('<i',img,C+a)[0]
def s32(v):
    v&=0xffffffff; return v-(1<<32) if v>>31 else v
n=23
K=[[i32(0x466f+20*j+4*c) for c in range(5)] for j in range(24)]
print('key0',K[0])
coef={}
for c in range(5):
    Q=(lambda v: v>>16) if c>=2 else (lambda v: v)
    Tg=[0]+[Q(s32(K[j+1][c]-K[j-1][c])) for j in range(1,n)]+[0]
    for j in range(n):
        d=Q(K[j][c]); e=Q(K[j+1][c]); c1=Tg[j]
        a=s32(2*d-2*e+Tg[j]+Tg[j+1]); b=s32(e-a-c1-d)
        coef[(j,c)]=(a,b,c1,d)
def ev(p):
    u=p&255; t2=u*u; t3=(t2*u)>>8; t1=u<<8; seg=p>>8
    out=[]
    for c in range(5):
        a,b,c1,d=coef[(seg,c)]
        v=s32((d<<16)+a*t3+b*t2+c1*t1)
        if c<2: v>>=16
        out.append(v)
    return out
for p in (2459,2461,2463,2465): print(p, ev(p))
print('want c4500 [17, 5, 2733752, 327680, -1976088]')
for p in (211,213,215): print(p, ev(p))
print('want c3300 [11, -46, -634810, 597650, -1507793]')
