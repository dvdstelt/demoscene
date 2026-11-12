"""p7: check the texel addressing of the bump-env filler (2046.. unclipped) against register probes
(run_dope.py --probe 0F74:2052@F:N,0F74:206C@F:N > probe7.log); the offset table is cs:0000 (static data)."""
import struct
import sys
from snap import img
b = img('p7')
TAB = [struct.unpack_from('<H', b, 0x100 + 2 * i)[0] for i in range(256)]
ok = {}
for line in open(sys.argv[1]):
    if not line.startswith('probe'):
        continue
    f = line.split()
    addr = f[1]
    r = {kv.split('=')[0]: int(kv.split('=')[1], 16) for kv in f[4:]}
    ecx, ebx, edx, esi, eax = r['ecx'], r['ebx'], r['edx'], r['esi'], r['eax']
    if addr == '0F74:2052':      # bump texel: row = (u >> 8) & 127, col = (v >> 8) & 127 (ecx = v << 16 | u)
        exp = ((((ecx & 0xffff) >> 8) & 127) << 8) | ((ecx >> 24) & 127)
        good = (ebx & 0xffff) == exp
    else:                        # env texel (row = u2 >> 8, col = v2 >> 8) and the offset word
        exp = (((edx >> 8) & 0xff) << 8) | (edx >> 24)
        good = (ebx & 0xffff) == exp and (esi & 0xffff) == TAB[eax & 0xff]
    ok.setdefault(addr, [0, 0])[0 if good else 1] += 1
print({a: 'ok %d bad %d' % tuple(v) for a, v in ok.items()})
