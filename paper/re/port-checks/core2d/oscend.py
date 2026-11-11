import sys, pickle, struct
sys.path.insert(0, '/home/dennis/Downloads/acme_303/paper/re')
import unicorn
seg = open('/home/dennis/Downloads/acme_303/paper/re/paper_img.bin','rb').read()[0xd00:]
T = list(struct.unpack_from('<64I', seg, 0x9a0)); i = seg[0xaa0] & 63; j = seg[0xaa1] & 63
NMAX = 300000
out = [0]*(NMAX+1)
for n in range(1, NMAX+1):
    v = (T[i] + T[j]) & 0xffffffff; T[j] = v; i = (i+1)&63; j = (j+1)&63; out[n] = v
s = pickle.load(open('../snaps/t11100.snap', 'rb'))
m = s['mem']; p = struct.unpack_from('<I', m, 0x1edd5)[0]
recs = [struct.unpack_from('<iii', m, p + 12*k) for k in range(72)]
a0, s0 = recs[0][1], recs[0][2]
for N in range(200000, NMAX - 300):
    if (out[N+2] & 0x3ff) + 0x100 == a0 and ((((out[N+3]&0xff)-0x80)*300)>>8) == s0 and (out[N+5] & 0x3ff) + 0x100 == recs[1][1]:
        ns = set()
        for k in range(72):
            sp = recs[k][2]
            if sp == 0: continue
            d = (recs[k][0] - out[N+1+3*k]) & 0xffffffff
            for n in range(0, 5000):
                if (sp*n - d) & 0xffffffff == 0: ns.add(n); break
        print('68fps run: end osc at N', N, 'advances by tick 11099:', ns)
