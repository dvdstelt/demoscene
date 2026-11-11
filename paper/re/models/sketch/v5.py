from mm import M
s = M(1250)
objs = [s.d(0x5db8 + 4*k) for k in range(12)]
for k, o in enumerate(objs):
    va = s.d(o+0x78); nv = s.d(o+0x6c)
    xs=[]; ys=[]; zs=[]
    for v in range(nv):
        p = va + v*0x24
        xs.append(s.i(p+0x18)/32); ys.append(s.i(p+0x1c)/32); zs.append(s.i(p+0x14)/65536)
    wx = [s.i(va+v*0x24) for v in range(nv)]; wy=[s.i(va+v*0x24+4) for v in range(nv)]; wz=[s.i(va+v*0x24+8) for v in range(nv)]
    print(k, 'screen x %.0f..%.0f y %.0f..%.0f camz %.0f..%.0f' % (min(xs),max(xs),min(ys),max(ys),min(zs),max(zs)), ' world x %d..%d y %d..%d z %d..%d' % (min(wx),max(wx),min(wy),max(wy),min(wz),max(wz)))
    fa = s.d(o+0x74); nf = s.d(o+0x70)
    import collections
    c = collections.Counter((s.by(fa+f*0x4c+0x1c), s.by(fa+f*0x4c+0x1e), s.by(fa+f*0x4c+0x1f)) for f in range(nf))
    print('   (1c,1e,1f) counts', sorted(c.items())[:12])
