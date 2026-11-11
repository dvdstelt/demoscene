import pickle, struct, sys
sys.path.insert(0,'/home/dennis/Downloads/acme_303/paper/re')
R='/tmp/claude-1000/-home-dennis--config-Claude-scratch-workspaces-e97b6b66-a77a-46ac-a12a-f43ab86d2564-b079650b-97d8-49d5-bd62-940ddce6ee3d-scratch-2026-11-10-9f02fa/0a51be05-fc78-48c4-9845-e78eac5ea96a/scratchpad/re-water/'
D='/tmp/claude-1000/-home-dennis--config-Claude-scratch-workspaces-e97b6b66-a77a-46ac-a12a-f43ab86d2564-b079650b-97d8-49d5-bd62-940ddce6ee3d-scratch-2026-11-10-9f02fa/0a51be05-fc78-48c4-9845-e78eac5ea96a/scratchpad/part-greetings/dump/'
W=pickle.load(open(R+'x.wrec','rb'))
mine={}
for line in open(D+'states.txt'):
    v=line.split(); t=int(v[0]); mine[t]=([int(v[1+2*k]) for k in range(4)],[int(v[2+2*k]) for k in range(4)],float(v[9]),float(v[10]))
names=[' ',' ',' ',' ','hello','     jamm','halcyon','   orange','   bullfrog','subliminal','     acme','valhalla','oxygene','    cubic','amen','     pulse']
img=open('/home/dennis/Downloads/acme_303/paper/re/paper_img.bin','rb').read()
def cstr(a):
    a+=0xd00; e=img.index(b'\0',a); return img[a:e].decode()
bad=0; n=0; seen=set()
for t in W['ticks']:
    tick=t[0]
    if tick<7744 or tick in seen: continue
    seen.add(tick); n+=1
    sl=t[6]; lv=[sl[4+5*s] for s in range(4)]; ptr=[struct.unpack_from('<I',sl,5+5*s)[0] for s in range(4)]
    st=[cstr(p) if p else None for p in ptr]
    ml,mn,pa,la=mine[tick]
    ms=[names[k] if k>=0 else None for k in mn]
    shown=max(2372,t[1]-1)
    ok = lv==ml and st==ms and shown==pa and t[3]==la
    if not ok:
        bad+=1
        if bad<10: print('tick',tick,'orig',lv,st,shown,t[3],'mine',ml,ms,pa,la)
print('ticks compared',n,'mismatches',bad)
