import pickle, struct, sys, numpy as np
sys.path.insert(0, '/home/dennis/Downloads/acme_303/paper/re')
import unicorn
S='/tmp/claude-1000/-home-dennis--config-Claude-scratch-workspaces-e97b6b66-a77a-46ac-a12a-f43ab86d2564-b079650b-97d8-49d5-bd62-940ddce6ee3d-scratch-2026-11-10-9f02fa/0a51be05-fc78-48c4-9845-e78eac5ea96a/scratchpad'
s=pickle.load(open(S+'/snaps/t7000.snap','rb')); m=s['mem']
i32=lambda a: struct.unpack_from('<i',m,a)[0]; u32=lambda a: struct.unpack_from('<I',m,a)[0]
print('snap tick', s['tick'], 'eip %x'%s['eip'])
ob=u32(0x6816); vl=u32(ob+0x78)
print('obj m', [i32(ob+4*k) for k in range(9)], 'pos', [i32(ob+0x48+4*k) for k in range(3)])
print('W (C)', [i32(ob+0x24+4*k) for k in range(9)], 'T', [i32(ob+0x54+4*k) for k in range(3)])
V=[i32(0x1e468+4*k) for k in range(9)]; cp=[i32(0x1ea9d+4*k) for k in range(3)]
M=np.array([i32(ob+4*k) for k in range(9)],float).reshape(3,3)/65536; P=np.array([i32(ob+0x48+4*k) for k in range(3)],float)
Vm=np.array(V,float).reshape(3,3)/65536; CP=np.array(cp,float)
for k in range(0,8):
    rec=[i32(vl+0x24*k+4*q) for q in range(8)]
    v=np.array(rec[:3],float)
    c=Vm@(M@v+P-CP)
    print(k, 'stored z %.1f sx %.2f sy %.2f'%(rec[5]/65536, rec[6]/32, rec[7]/32), ' float: z %.1f sx %.2f sy %.2f'%(c[2],160+224*c[0]/c[2],100+224*c[1]/c[2]))
# the history ring: camera for previous ticks
FR=64000+768
data=np.memmap('/home/dennis/Downloads/acme_303/paper/re/full.frames',dtype=np.uint8,mode='r')
back=u32(0x1edf9); bb=np.frombuffer(m[back:back+64000],np.uint8); vga=np.frombuffer(m[0xa0000:0xa0000+64000],np.uint8)
for t in range(6996,7004):
    f=np.asarray(data[t*FR:t*FR+64000]); print(t,'vs back',(f!=bb).sum(),'vs vga',(f!=vga).sum())
