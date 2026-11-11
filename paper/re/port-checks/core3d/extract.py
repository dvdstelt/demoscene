import pickle, struct, sys
S='/tmp/claude-1000/-home-dennis--config-Claude-scratch-workspaces-e97b6b66-a77a-46ac-a12a-f43ab86d2564-b079650b-97d8-49d5-bd62-940ddce6ee3d-scratch-2026-11-10-9f02fa/0a51be05-fc78-48c4-9845-e78eac5ea96a/scratchpad'
def rd(m):
    i32=lambda a: struct.unpack_from('<i',m,a)[0]
    u32=lambda a: struct.unpack_from('<I',m,a)[0]
    return i32,u32
for name in ['c3300','c4500','d4500','e4500','c5500']:
    try: cap=pickle.load(open(S+'/re-scene3d/%s.cap0.pkl'%name,'rb'))
    except Exception as e: print(name, 'no', e); continue
    m=cap['memA']; i32,u32=rd(m)
    print(name, 'tick',cap['tick'],'A %x B %x'%(cap['A'],cap['B']))
    print('  cam yaw pitch x y z', [i32(0x5de8+4*k) for k in range(5)], 'campos>>10', [i32(0x5df0+4*k)>>10 for k in range(3)])
    print('  p', u32(0x482b), 'mode', m[0x1ea94], 'pencil', [i32(a) for a in (0x55d6,0x55da,0x55de,0x55d2)], 'thr', m[0x7324], 'hwend', u32(0x4837), 'hwstart', u32(0x483b))
    print('  noise', [i32(0x71b7+4*k) for k in range(20)])
