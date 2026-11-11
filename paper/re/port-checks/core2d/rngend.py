import struct
img = open('/home/dennis/Downloads/acme_303/paper/re/paper_img.bin','rb').read()
seg = img[0xd00:]
T = list(struct.unpack_from('<64I', seg, 0x9a0)); i = seg[0xaa0] & 63; j = seg[0xaa1] & 63
NMAX = 600000
out = [0]*(NMAX+1)
for n in range(1, NMAX+1):
    v = (T[i] + T[j]) & 0xffffffff; T[j] = v; i = (i+1)&63; j = (j+1)&63; out[n] = v
def osc(N, scale):
    r = []
    for k in range(72):
        ph = out[N+1+3*k] & 0xffff; amp = (out[N+2+3*k] & 0x3ff) + 0x100
        b = out[N+3+3*k] & 0xff
        sp = ((b - 0x80) * scale) >> 8
        r.append((ph, amp, sp))
    return r
# title check
t = osc(140057+192, 300)
print('title first', [hex(x[0]) for x in t[:4]], [x[1] for x in t[:4]], [x[2] for x in t[:4]])
for N in range(179785, NMAX-300):
    if out[N+1] & 0xffff == 0xf801 and (out[N+2]&0x3ff)+0x100 == 987:
        o = osc(N, 300)
        print('end osc at N', N, 'letters at', N-192, [hex(x[0]) for x in o[:4]], [x[1] for x in o[:4]], [x[2] for x in o[:4]], 'last', hex(o[71][0]), o[71][1], o[71][2])
# water check
w = osc(179569, 200)
print('water', [(x[0]) for x in w[:4]], [x[1] for x in w[:4]], [x[2] for x in w[:4]])
