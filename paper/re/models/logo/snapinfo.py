import sys, pickle, struct
sys.path.insert(0, '/home/dennis/Downloads/acme_303/paper/re')
import unicorn
S = '/tmp/claude-1000/-home-dennis--config-Claude-scratch-workspaces-e97b6b66-a77a-46ac-a12a-f43ab86d2564-b079650b-97d8-49d5-bd62-940ddce6ee3d-scratch-2026-11-10-9f02fa/0a51be05-fc78-48c4-9845-e78eac5ea96a/scratchpad'
for t in sys.argv[1:]:
    snap = pickle.load(open(f'{S}/snaps/t{t}.snap', 'rb'))
    m = snap['mem']
    d = lambda a: struct.unpack_from('<I', m, a)[0]
    di = lambda a: struct.unpack_from('<i', m, a)[0]
    print('tick', snap['tick'], 'keys', list(snap.keys()))
    for name, a in [('page 1edc5', 0x1edc5), ('tex 1edf5', 0x1edf5), ('lut 1eded', 0x1eded), ('bb 1edf9', 0x1edf9),
                    ('list 1edfd', 0x1edfd), ('cnt 1edb1', 0x1edb1), ('1edad', 0x1edad), ('lvl 1edb5', 0x1edb5),
                    ('tgt 1edbd', 0x1edbd), ('step 1edb9', 0x1edb9), ('tick 1edc1', 0x1edc1), ('cb 1e5ef', 0x1e5ef),
                    ('mus 1ea8b', 0x1ea8b), ('end 1ea8f', 0x1ea8f), ('21455', 0x21455), ('1eaa9', 0x1eaa9),
                    ('23a0', 0x23a0), ('23a4', 0x23a4), ('1ede1', 0x1ede1), ('1ede9', 0x1ede9), ('1eddd', 0x1eddd), ('eb8', 0xeb8)]:
        print(f'  {name:12s} {d(a):#010x} {di(a)}')
    print('  rng idx', m[0xaa0], m[0xaa1])
    pickle.dump({'mem_parts': {k: m[a:a+n] for k, a, n in []}}, open('/dev/null', 'wb'))
