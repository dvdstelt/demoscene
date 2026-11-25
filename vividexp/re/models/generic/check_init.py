"""Check GenericInit's tables against snapshot memory: splines (y2), sine table, envmap table, object state."""
import sys
import gen
from snap import snap

fr = int(sys.argv[1]) if len(sys.argv) > 1 else 5951
ini = sys.argv[2] if len(sys.argv) > 2 else 'gen/gen.ini'
m = snap(fr)
nobj = m.u32(0x834468)
objs = m.u32(0x834478)
print('frame', fr, 'objects', nobj, 'at', hex(objs), 'counter 0x832500 =', m.u32(0x832500), 'frame ctr', m.u32(0x825c5c))
print('looping', m.u32(0x83446c), 'common spline', m.u32(0x834470), 'bgpic', m.u32(0x834450), 'bgcol', hex(m.u32(0x834458)))
cfg = gen.read_ini(ini)
for i in range(nobj):
    o = objs + 0x74 * i
    name = m.rd(o + 0x50, 32).split(b'\0')[0].decode()
    f = [m.s32(o + k) for k in range(0, 0x74, 4)]
    print(name, 'type', f[0x48 // 4], 'len', f[0x34 // 4], 'speed', f[0x38 // 4], 'pos', f[0x3c // 4], 'max', hex(f[0x40 // 4]),
          'start', f[0x44 // 4], 't0', f[0x4c // 4], 'xyz abc', f[7:13], 'draw', [hex(x & 0xffffffff) for x in f[0x60 // 4:0x74 // 4]])
    spl = cfg.get(name + 'SplinePath', cfg.get('CommonSplinePath'))
    cols = gen.read_spl(spl)
    ok = True
    for c in range(6):
        rec = gen.spline_prep(cols[c])
        a = m.u32(o + 4 + 4 * c)
        n = f[0x34 // 4]
        for k in range(n):
            x, y, y2 = m.s32(a + 16 * k), m.s32(a + 16 * k + 4), m.s32(a + 16 * k + 8)
            if (x, y, y2) != (rec[0][k], rec[1][k], rec[2][k]):
                ok = False
                print('  col', c, 'key', k, 'snap', (x, y, y2), 'model', (rec[0][k], rec[1][k], rec[2][k]))
                break
    print('  splines match:', ok)
# sine table
t = m.u32(0x844d20)
bad = [k for k in range(0x401) if m.s32(t + 4 * k) != gen.SINTAB[k]]
print('sine table', hex(t), 'mismatches', len(bad), bad[:5], [(m.s32(t + 4 * k), gen.SINTAB[k]) for k in bad[:5]])
et = gen.env_table()
bad = [i for i in range(-256, 257) if et[i] != gen.env_formula(i)]
print('env table mismatches vs round(8192+16384/pi*asin(i/256))', len(bad), [(i, et[i], gen.env_formula(i)) for i in bad[:8]])
