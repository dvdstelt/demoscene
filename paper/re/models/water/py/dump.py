import pickle, struct, sys, numpy as np
import unicorn
SN='/tmp/claude-1000/-home-dennis--config-Claude-scratch-workspaces-e97b6b66-a77a-46ac-a12a-f43ab86d2564-b079650b-97d8-49d5-bd62-940ddce6ee3d-scratch-2026-11-10-9f02fa/0a51be05-fc78-48c4-9845-e78eac5ea96a/scratchpad/snaps/'
OUT='/tmp/claude-1000/-home-dennis--config-Claude-scratch-workspaces-e97b6b66-a77a-46ac-a12a-f43ab86d2564-b079650b-97d8-49d5-bd62-940ddce6ee3d-scratch-2026-11-10-9f02fa/0a51be05-fc78-48c4-9845-e78eac5ea96a/scratchpad/re-water/'
t=sys.argv[1]
s=pickle.load(open(SN+'t%s.snap'%t,'rb'))
m=s['mem']
r32=lambda a: struct.unpack_from('<I',m,a)[0]
ri32=lambda a: struct.unpack_from('<i',m,a)[0]
print('tick',s['tick'],'eip',hex(s['eip']))
for name,a in [('lightmap',0x1edd1),('tex',0x1eddd),('shade',0x1eded),('hmap',0x1edc9),('glyph',0x1edcd),('font',0x1ede9),('waves',0x1edd5),('grid',0x1edd9),('scratch',0x1edfd),('vga',0x1edf1),('back',0x1edf9),('heap8',0x8)]:
    print(name, hex(r32(a)))
for name,a in [('lightang',0x22469),('palang',0x14dd),('counter',0x1ea8b),('target',0x1ea8f),('level',0x1edb5),('lvtarget',0x1edbd),('lvstep',0x1edb9),('tempo_acc',0x1e5fb),('tempo',0x1e5ff),('slot',0x2460),('nextstr',0x22465),('ticks',0x1edc1),('22461',0x22461)]:
    print(name, hex(r32(a)), ri32(a))
print('speed', m[0x1e4e5], 'sub', m[0x1e4e6], 'state', m[0x1ea94], 'shift1505', m[0x1505], 'efc', m[0x2efc], m[0x2f02], 'eb8', hex(r32(0xeb8)))
for i in range(4):
    a=0x2464+5*i; print('slot',i,m[a],hex(r32(a+1)))
np.save(OUT+'mem%s_lightmap.npy'%t, np.frombuffer(m, np.uint8, 512*256, r32(0x1edd1)).reshape(256,512))
np.save(OUT+'mem%s_tex.npy'%t, np.frombuffer(m, np.uint8, 0x20000, r32(0x1eddd)).reshape(512,256))
np.save(OUT+'mem%s_shade.npy'%t, np.frombuffer(m, np.uint8, 0x20000, r32(0x1eded)).reshape(2,256,256))
np.save(OUT+'mem%s_hmap.npy'%t, np.frombuffer(m, np.uint8, 320*202, r32(0x1edc9)).reshape(202,320))
np.save(OUT+'mem%s_glyph.npy'%t, np.frombuffer(m, np.uint8, 26*0xd00, r32(0x1edcd)).reshape(26*64,52))
np.save(OUT+'mem%s_font.npy'%t, np.frombuffer(m, np.uint8, 26*2048, r32(0x1ede9)).reshape(26,2048))
np.save(OUT+'mem%s_waves.npy'%t, np.frombuffer(m, np.int32, 72*3, r32(0x1edd5)).reshape(72,3))
np.save(OUT+'mem%s_grid.npy'%t, np.frombuffer(m, np.int32, 41*26*3, r32(0x1edd9)).reshape(26,41,3))
np.save(OUT+'mem%s_vga.npy'%t, np.frombuffer(m, np.uint8, 64000, 0xa0000).reshape(200,320))
np.save(OUT+'mem%s_pal.npy'%t, np.frombuffer(s['pal'], np.uint8).reshape(256,3))
np.save(OUT+'mem%s_palbuf.npy'%t, np.frombuffer(m, np.uint8, 768, 0x1eaad).reshape(256,3))
