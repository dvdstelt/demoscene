import ast, struct, numpy as np
rows = [ast.literal_eval(l) for l in open('strokes.txt') if l.startswith('(')]
K = np.float32(struct.unpack('<f', open('/home/dennis/Downloads/acme_303/paper/re/paper_img.bin','rb').read()[0xd00+0x483f:0xd00+0x483f+4])[0])
print('const', repr(float(K)))
def nsteps(sx, sy, ex, ey, t):
    d2 = (sx-ex)**2 + (sy-ey)**2
    v = np.sqrt(np.sqrt(np.float64(d2))) * np.float64(K)   # x87 extended; close enough
    n = int(np.rint(v)) - 3
    if n < 0: n = 0
    return n << t
st = dict(x=223, y=120, z=-366, step=0, n=0, sx=0, sy=0, ex=0, ey=0, idx=0, t=0)
log = []
tick = 1304
starts = {}
while True:
    s = st
    if s['z'] < 0:
        s['z'] += 10
        if s['z'] > 0: s['z'] = 0
    while True:
        if s['step'] < s['n']:
            r = 65536 // s['n']
            s['x'] = s['sx'] + (((s['ex']-s['sx'])*s['step']*r) >> 16)
            s['y'] = s['sy'] + (((s['ey']-s['sy'])*s['step']*r) >> 16)
            s['step'] += 1
            break
        if s['idx'] >= 0x113: break
        e = rows[s['idx']]
        ax, ay, bx, by = e[7:11]
        s['step'] = 0
        s['t'] ^= 1
        if s['t']:
            s['sx'], s['sy'], s['ex'], s['ey'] = s['x'], s['y'], ax, ay
        else:
            s['sx'], s['sy'], s['ex'], s['ey'] = ax, ay, bx, by
            starts[s['idx']] = tick
            s['idx'] += 1
        s['n'] = nsteps(s['sx'], s['sy'], s['ex'], s['ey'], s['t'])
    if tick == 2000: print('tick 2000 state', s)
    if s['idx'] >= 0x113:
        print('idx reached 0x113 at the IRQ of tick', tick); break
    tick += 1
import json
json.dump(starts, open('starts.json','w'))
for i in [0,1,2,10,50,100,127,128,200,274]: print(i, starts[i])
