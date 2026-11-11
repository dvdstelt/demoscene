# Dump the original's object records (vertices, faces: v, nb, static flags, base, add, normal) for comparison with
# the remake's loaders. Scene objects from the c4500 capture, planes from the t7000 / t9500 snapshots.
import pickle, struct, sys
sys.path.insert(0, '/home/dennis/Downloads/acme_303/paper/re')
S='/tmp/claude-1000/-home-dennis--config-Claude-scratch-workspaces-e97b6b66-a77a-46ac-a12a-f43ab86d2564-b079650b-97d8-49d5-bd62-940ddce6ee3d-scratch-2026-11-10-9f02fa/0a51be05-fc78-48c4-9845-e78eac5ea96a/scratchpad'
out=open('orig_objs.txt','w')
def dump(m, ob, name, verts=True):
    i32=lambda a: struct.unpack_from('<i',m,a)[0]; u32=lambda a: struct.unpack_from('<I',m,a)[0]
    nv,nf=i32(ob+0x6c),i32(ob+0x70); vl,fl=u32(ob+0x78),u32(ob+0x74)
    out.write('obj %s %d %d\n'%(name,nv,nf))
    if verts:
        for k in range(nv): out.write('v %d %d %d\n'%tuple(i32(vl+0x24*k+4*q) for q in range(3)))
    for k in range(nf):
        fa=fl+0x4c*k
        v=[(u32(fa+4*q)-vl)//0x24 for q in range(3)]; nb=[(u32(fa+0xc+4*q)-fl)//0x4c for q in range(3)]
        out.write('f %d %d %d  %d %d %d  %d %d %d  %d %d %d\n'%(*v,*nb,(m[fa+0x1d]>>3)&7,m[fa+0x1e],m[fa+0x1c],*(i32(fa+0x24+4*q) for q in range(3))))
cap=pickle.load(open(S+'/re-scene3d/c4500.cap0.pkl','rb'))
m=cap['memA']
names=['sun','path','house','pole-left','pole-right','tree5','tree6','tree7','banner','windows','flower','pencil']
for s in range(12):
    ob=struct.unpack_from('<I',m,0x5db8+4*s)[0]
    dump(m, ob, names[s], verts=(s!=8))
import unicorn
s=pickle.load(open(S+'/snaps/t7000.snap','rb')); m=s['mem']
dump(m, struct.unpack_from('<I',m,0x6816)[0], 'teal-plane')
s=pickle.load(open(S+'/snaps/t9500.snap','rb')); m=s['mem']
dump(m, struct.unpack_from('<I',m,struct.unpack_from('<I',m,0x33c0)[0])[0], 'flock-plane')
out.close()
print(open('orig_objs.txt').read().count('obj'))
