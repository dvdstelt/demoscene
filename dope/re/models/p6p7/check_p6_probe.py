"""p6: check the texel addressing of the bump-env filler (1db0: unclipped 1e16.., clipped 1f36..) against register
probes (run_dope.py --probe 0F74:1E22@F:N,0F74:1E3C@F:N,0F74:1F42@F:N,0F74:1F5B@F:N > probe6.log)."""
import sys
ok = {}
for line in open(sys.argv[1]):
    if not line.startswith('probe'):
        continue
    f = line.split()
    addr = f[1]
    r = {kv.split('=')[0]: int(kv.split('=')[1], 16) for kv in f[4:]}
    ecx, ebx, edx, esi, eax = r['ecx'], r['ebx'], r['edx'], r['esi'], r['eax']
    if addr in ('0F74:1E22', '0F74:1F42'):        # bump texel address from the mesh UV (ecx = v1 << 16 | u1)
        u1, v1 = ecx & 0xffff, ecx >> 16
        exp = ((((u1 >> 8) & 31) << 2 | (v1 >> 14)) << 8) | ((v1 >> 6) & 127)
        good = (ebx & 0xffff) == exp
    else:                                        # env texel address (edx = v2 << 16 | u2) and the offset word
        k = 0x100
        by = [(((-8 * k + i * k) & 0xffff) >> 8) for i in range(16)]
        al = eax & 0xff
        exp = (((edx >> 8) & 0xff) << 8) | (edx >> 24)
        good = (ebx & 0xffff) == exp and (esi & 0xffff) == ((by[al >> 4] << 8) | by[al & 15])
    ok.setdefault(addr, [0, 0])[0 if good else 1] += 1
print({a: 'ok %d bad %d' % tuple(v) for a, v in ok.items()})
