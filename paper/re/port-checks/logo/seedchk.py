import numpy as np
FR=64768
d=np.memmap('/home/dennis/Downloads/acme_303/paper/re/full.frames',dtype=np.uint8,mode='r')
def fr(t): return np.asarray(d[t*FR:t*FR+64000]).reshape(200,320)
seeds=[(61,104),(71,78),(96,114),(146,127),(214,117),(195,74),(257,89),(260,110)]
for (x,y) in seeds:
    for t in range(5670,5930):
        if 0x41<=fr(t)[y,x]<=0x4f:
            print((x,y),'first ink at record',t, 'ink count in rec', ((fr(t)>=0x41)&(fr(t)<=0x4f)).sum()); break
