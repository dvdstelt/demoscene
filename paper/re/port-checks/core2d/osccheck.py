import sys, pickle, struct
sys.path.insert(0, '/home/dennis/Downloads/acme_303/paper/re')
import unicorn
seg = open('/home/dennis/Downloads/acme_303/paper/re/paper_img.bin','rb').read()[0xd00:]
T = list(struct.unpack_from('<64I', seg, 0x9a0)); i = seg[0xaa0] & 63; j = seg[0xaa1] & 63
NMAX = 300000
out = [0]*(NMAX+1)
for n in range(1, NMAX+1):
    v = (T[i] + T[j]) & 0xffffffff; T[j] = v; i = (i+1)&63; j = (j+1)&63; out[n] = v
def osc(N, scale):
    r = []
    for k in range(72):
        ph = out[N+1+3*k]; amp = (out[N+2+3*k] & 0x3ff) + 0x100
        sp = (((out[N+3+3*k] & 0xff) - 0x80) * scale) >> 8
        r.append((ph, amp, sp))
    return r
for snap, N, scale, per in (('t120', 140249, 300, 1), ('t1050', 140249, 300, 1), ('t6100', 162969, 300, 2), ('t7000', 162969, 300, 2), ('t8000', 179569, 200, 1), ('t8700', 179569, 200, 1)):
    try: s = pickle.load(open('../snaps/%s.snap' % snap, 'rb'))
    except FileNotFoundError: continue
    m = s['mem']; p = struct.unpack_from('<I', m, 0x1edd5)[0]
    o = osc(N, scale)
    ns = set()
    for k in range(72):
        ph, amp, sp = struct.unpack_from('<iii', m, p + 12*k)
        assert (amp, sp) == (o[k][1], o[k][2]), (snap, k, amp, sp, o[k])
        if o[k][2] == 0: continue
        d = (ph - o[k][0]) & 0xffffffff
        # solve d = sp*per*n mod 2^32
        for n in range(0, 20000):
            if (sp*per*n - d) & 0xffffffff == 0: ns.add(n); break
        else: ns.add(None)
    print(snap, 'tick', s['tick'], 'advances (in units of per-tick steps)', ns)
