import pickle, struct
S='/tmp/claude-1000/-home-dennis--config-Claude-scratch-workspaces-e97b6b66-a77a-46ac-a12a-f43ab86d2564-b079650b-97d8-49d5-bd62-940ddce6ee3d-scratch-2026-11-10-9f02fa/0a51be05-fc78-48c4-9845-e78eac5ea96a/scratchpad'
cap=pickle.load(open(S+'/re-scene3d/c4500.cap0.pkl','rb')); m=cap['memB']
i32=lambda a: struct.unpack_from('<i',m,a)[0]; u32=lambda a: struct.unpack_from('<I',m,a)[0]
mine={}; cur=None
for l in open(S+'/core3d/my_exact.txt'):
    p=l.split()
    if p[0]=='obj': cur=int(p[1]); mine[cur]={'v':[],'f':[]}
    else: mine[cur][p[0]].append(tuple(int(x) for x in p[1:]))
tv=bv=tf=bf=0
for s in range(11):
    ob=u32(0x5db8+4*s); vl=u32(ob+0x78); fl=u32(ob+0x74); nv=i32(ob+0x6c); nf=i32(ob+0x70)
    for k in range(nv):
        cz,sx,sy=i32(vl+0x24*k+0x14),i32(vl+0x24*k+0x18),i32(vl+0x24*k+0x1c)
        z,x,y=mine[s]['v'][k]; tv+=1
        if cz!=z or (cz>0x1900000 and (sx,sy)!=(x,y)): bv+=1
    for k in range(nf):
        fa=fl+0x4c*k; F=m[fa+0x1d]; C=m[fa+0x1f]; f,c=mine[s]['f'][k]; tf+=1
        if (F&0x80)!=(f&0x80) or (not F&0x80 and (F,C)!=(f,c)): bf+=1; print('face',s,k,hex(F),C,hex(f),c)
print('vertices %d mismatches %d; faces %d mismatches %d'%(tv,bv,tf,bf))
