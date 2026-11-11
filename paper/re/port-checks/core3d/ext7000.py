import pickle, struct, sys
sys.path.insert(0, '/home/dennis/Downloads/acme_303/paper/re')
import unicorn
S='/tmp/claude-1000/-home-dennis--config-Claude-scratch-workspaces-e97b6b66-a77a-46ac-a12a-f43ab86d2564-b079650b-97d8-49d5-bd62-940ddce6ee3d-scratch-2026-11-10-9f02fa/0a51be05-fc78-48c4-9845-e78eac5ea96a/scratchpad'
for t in [7000, 9500]:
    s=pickle.load(open(S+'/snaps/t%d.snap'%t,'rb'))
    m=s['mem']
    print(t, 'keys', list(s.keys())[:10])
    i32=lambda a: struct.unpack_from('<i',m,a)[0]; u32=lambda a: struct.unpack_from('<I',m,a)[0]
    print(' V', [i32(0x1e468+4*k) for k in range(9)], 'campos', [i32(0x1ea9d+4*k) for k in range(3)], 'mode', m[0x1ea94], 'shift', m[0x42ac])
    obs=[u32(0x6816)] if t<8000 else [u32(u32(0x33c0)+32*n) for n in range(32)]
    for ob in obs[:3]:
        print(' obj m', [i32(ob+4*k) for k in range(9)], 'pos', [i32(ob+0x48+4*k) for k in range(3)], 'Lo', [i32(ob+0x60+4*k) for k in range(3)], 'nv nf', i32(ob+0x6c), i32(ob+0x70))
    print(' ticks counter', i32(0x1ea8b))
