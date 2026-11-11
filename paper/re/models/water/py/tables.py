import pickle, numpy as np
O='/tmp/claude-1000/-home-dennis--config-Claude-scratch-workspaces-e97b6b66-a77a-46ac-a12a-f43ab86d2564-b079650b-97d8-49d5-bd62-940ddce6ee3d-scratch-2026-11-10-9f02fa/0a51be05-fc78-48c4-9845-e78eac5ea96a/scratchpad/re-water/'
W=pickle.load(open(O+'x.wrec','rb'))
w=np.frombuffer(W['waves0'],np.int32).reshape(72,3).astype(np.int64)
lines=[]
for j in range(3):
    for i in range(3):
        b=(j*3+i)*8
        xs=' '.join('{%5d,%4d,%4d}'%(w[b+2*o,0]&0xffff,w[b+2*o,1],w[b+2*o,2]) for o in range(4))
        ys=' '.join('{%5d,%4d,%4d}'%(w[b+2*o+1,0]&0xffff,w[b+2*o+1,1],w[b+2*o+1,2]) for o in range(4))
        lines.append('  /* P[%d][%d].x */ %s,'%(j,i,xs))
        lines.append('  /* P[%d][%d].y */ %s,'%(j,i,ys))
print('\n'.join(lines))
font=np.load(O+'gen_font.npy')
W_=[int(font[g*2048]) for g in range(26)]
names=['hello','     jamm','halcyon','   orange','   bullfrog','subliminal','     acme','valhalla','oxygene','    cubic','amen','     pulse']
for n in names:
    sp=len(n)-len(n.lstrip()); x0=5+30*sp; wsum=sum(W_[ord(c)-97]+4 for c in n.strip())
    print(n.strip(), 'x0', x0, 'glyph left', x0+2, 'end', x0+wsum, )
# font vertical extent
f=font[:26*2048].reshape(26,2048)
for g in range(26):
    rows=[r for r in range(32) if f[g,1+r*60:1+r*60+60].any()]
    print(chr(97+g), W_[g], rows[0], rows[-1], end='; ')
