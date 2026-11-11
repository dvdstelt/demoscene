import numpy as np
FR=64000+768
data=np.memmap('/home/dennis/Downloads/acme_303/paper/re/full.frames',dtype=np.uint8,mode='r')
def rec(t): return np.asarray(data[t*FR:t*FR+64000]).reshape(200,320)
for t in range(5474,5490):
    print(t, (rec(t)!=rec(t-1)).sum())
for t in range(3183,3192):
    print(t, (rec(t)!=rec(t-1)).sum())
