import pickle
from cmp1 import MM
dumps = pickle.load(open('d2000.dumps', 'rb'))
s = MM([d for d in dumps if d['addr']==0x604a and 'mem' in d][0]['mem'])
print(s.ints(0x1e468, 9))
SIN = [s.i(0x22479 + 4*i) for i in range(1024)]
print(SIN[20], SIN[276], SIN[(-28)&1023], SIN[(-28+256)&1023])
print(s.ints(0x5de8,5), s.ints(0x1ea9d,3))
