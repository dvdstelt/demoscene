import numpy as np, sys
FR=64768
a=np.memmap(sys.argv[1],dtype=np.uint8,mode='r')
b=np.memmap(sys.argv[2],dtype=np.uint8,mode='r')
off=int(sys.argv[3]); n=len(b)//FR
diff=[t+off for t in range(0,n,25) if not np.array_equal(a[(off+t)*FR:(off+t+1)*FR], b[t*FR:(t+1)*FR])]
print('compared', len(range(0,n,25)), 'differing', len(diff), diff[:8])
