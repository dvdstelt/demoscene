import sys, struct, pickle, math
sys.path.insert(0, '../..')
import numpy as np, unicorn, rep
v = rep.View('code32'); img = v.img
for name in ['part_38469', 'part_2c26f', 'part_3554b', 'part_38520']:
    mem = pickle.load(open('../../snaps/%s.snap' % name, 'rb'))['mem']
    s = np.frombuffer(bytes(mem[0x8fc0+0x24a3c:0x8fc0+0x24a3c+4*6000]), '<i4')
    nz = np.nonzero(s)[0]
    print(name, 'nonzero up to', nz.max() if len(nz) else None, s[:4], s[1020:1030], s[4094:4100], s[5118:5124])
f = np.frombuffer(img[0xec0+0x24a3c:0xec0+0x24a3c+4*1100], '<i4'); print('file', f[1020:1030])
mem = pickle.load(open('../../snaps/part_3554b.snap', 'rb'))['mem']
s = np.frombuffer(bytes(mem[0x8fc0+0x24a3c:0x8fc0+0x24a3c+4*5200]), '<i4').astype(np.int64)
i = np.arange(5200)
for k in [1024]:
    m = np.round(k*np.sin(2*np.pi*i/4096)).astype(np.int64)
    print('round 1024 sin diff', np.abs(s[:5121]-m[:5121]).max(), np.nonzero(s[:5121]-m[:5121])[0][:20])
print('cos at 0x25a40 = index 1025:', s[1025], s[1024], s[1023])
