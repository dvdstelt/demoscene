# Survey of the pictures (raw headers, index ranges) and tables (ranges, distinct values)
import numpy as np, os, sys
F1='unpacked/files/1/'; F2='unpacked/files/2/'
def raw(p):
    d=open(p,'rb').read()
    hdr=d[:32]
    w=int.from_bytes(hdr[8:10],'big'); h=int.from_bytes(hdr[10:12],'big')
    return hdr, np.frombuffer(d[32:32+768],np.uint8).reshape(256,3), np.frombuffer(d[800:],np.uint8), w,h
for p in sorted([F1+'pics/'+f for f in os.listdir(F1+'pics') if f.endswith('.raw')]+[F2+'pics/'+f for f in os.listdir(F2+'pics') if f.endswith('.raw')]):
    hdr,pal,px,w,h=raw(p)
    u=np.unique(px)
    print(p, hdr[:16].hex(), w,h, len(px), 'min',px.min(),'max',px.max(),'nuniq',len(u))
for d in [F1+'tabs/',F2+'tabs/']:
    for f in sorted(os.listdir(d)):
        t=np.frombuffer(open(d+f,'rb').read(),np.uint8)
        print(d+f, len(t), t.min(), t.max(), len(np.unique(t)))
