import pickle, sys, unicorn
SN='/tmp/claude-1000/-home-dennis--config-Claude-scratch-workspaces-e97b6b66-a77a-46ac-a12a-f43ab86d2564-b079650b-97d8-49d5-bd62-940ddce6ee3d-scratch-2026-11-10-9f02fa/0a51be05-fc78-48c4-9845-e78eac5ea96a/scratchpad/'
for t in sys.argv[1:]:
    s = pickle.load(open(SN+'snaps/t%s.snap' % t, 'rb'))
    open(SN+'re-sketch/m%s.bin' % t, 'wb').write(s['mem'][:0x1000000])
    open(SN+'re-sketch/p%s.bin' % t, 'wb').write(s['pal'])
    print(t, s['tick'], hex(s['eip']), s['st'])
