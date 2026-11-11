import sys, pickle, numpy as np
R='/tmp/claude-1000/-home-dennis--config-Claude-scratch-workspaces-e97b6b66-a77a-46ac-a12a-f43ab86d2564-b079650b-97d8-49d5-bd62-940ddce6ee3d-scratch-2026-11-10-9f02fa/0a51be05-fc78-48c4-9845-e78eac5ea96a/scratchpad/re-water/'
D='/tmp/claude-1000/-home-dennis--config-Claude-scratch-workspaces-e97b6b66-a77a-46ac-a12a-f43ab86d2564-b079650b-97d8-49d5-bd62-940ddce6ee3d-scratch-2026-11-10-9f02fa/0a51be05-fc78-48c4-9845-e78eac5ea96a/scratchpad/part-greetings/dump/'
h=np.fromfile(D+'h_7997.f32',np.float32).reshape(200,320)[::-1].astype(np.float64)
H=np.load(R+'mem8000_hmap.npy').astype(np.float64)[:200]
W=pickle.load(open(R+'x.wrec','rb'))
for t in W['ticks']:
    if t[0] in (7996,7997,7998):
        sl=t[6]; print(t[0],'levels',[sl[4+5*s] for s in range(4)])
# letters region: rows 0..60 (line 0, "hello"), and line 1 ("jamm")
for (y0,y1,x0,x1,nm) in ((0,64,0,160,'hello'),(46,110,150,320,'jamm')):
    a=h[y0:y1,x0:x1]; b=H[y0:y1,x0:x1]
    m=b>100
    print(nm,'max mine %.1f orig %.1f'%(a.max(),b.max()),'mean|d| on letters %.2f, mean d %.2f'%(np.abs(a-b)[m].mean(),(a-b)[m].mean()))
    for dy in (-1,0,1):
        for dx in (-1,0,1):
            aa=h[y0+1+dy:y1-1+dy,x0+1+dx:x1-1+dx]; bb=H[y0+1:y1-1,x0+1:x1-1]; mm=bb>100
            print('   shift',dx,dy,'%.2f'%np.abs(aa-bb)[mm].mean())
# print a profile row through 'h' stem of hello
y=20
print('row',y,'orig',H[y,0:40].astype(int).tolist()); print('row',y,'mine',np.round(h[y,0:40]).astype(int).tolist())
x=10
print('col',x,'orig',H[0:60,x].astype(int).tolist()); print('col',x,'mine',np.round(h[0:60,x]).astype(int).tolist())
for y in (36,40):
    print('row',y,'orig',H[y,0:40].astype(int).tolist()); print('row',y,'mine',np.round(h[y,0:40]).astype(int).tolist())
f=np.load(R+'mem8000_font.npy')
g=f[ord('h')-97]; w=g[0]; bits=g[1:1+32*60].reshape(32,60)
for r in range(4,22): print(r, ''.join('#' if bits[r,c] else '.' for c in range(w)))
