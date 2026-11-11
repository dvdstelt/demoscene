"""Model of the camera path 0x15bef -> 0x159fb / 0x15a59 (+ 0x15c40 roll wobble) for both parts, checked against
p1_hooks.pkl / p2_hooks.pkl (values at 0x15c24, before 0x154bc).

t = time * speed (speed = float at object header +0, times 1e-9 (double), stored as float)
pos(a, s) = s * (P0 cos(phi) cos(th), P1 sin(phi), P0 cos(phi) sin(th)),  th = P4 a + P6 sin(P4 a),
            phi = P7 sin(P5 a)
eye = pos(t, 1); target = pos(t + P3, P2); roll = P8 (t + P3) [+ pi sin(0.21 t) + pi sin(0.62 t) + sin(0.14 t)]"""
import math, pickle, struct, sys
import numpy as np
sys.path.insert(0, '/home/dennis/src/dvdstelt/demoscene/jizz/re')
import rep
F = np.float32
PSETS = {0: [rep.rdf(0x15afc + 4 * k) for k in range(9)], 2: [rep.rdf(0x15afc + 72 + 4 * k) for k in range(9)]}
SPEED = {0: F(F(rep.rdf(0x157a4)) * 1e-9), 2: F(F(rep.rdf(0x15945)) * 1e-9)}

def pos(a, s, P):
    P = [F(p) for p in P]
    x = F(a * P[4]); th = F(x + F(F(math.sin(x)) * P[6]))
    phi = F(F(math.sin(F(a * P[5]))) * P[7])
    cp = F(math.cos(phi))
    return (F(F(F(F(cp * F(math.cos(th))) * P[0]) * s)), F(F(F(math.sin(phi)) * P[1]) * s),
            F(F(F(cp * F(math.sin(th))) * P[0]) * s))

def camera(time, mode, wobble, t=None):
    P = PSETS[mode]
    if t is None:
        t = F(F(time) * SPEED[mode])
    eye = pos(t, F(1), P)
    a2 = F(t + F(P[3]))
    tgt = pos(a2, F(P[2]), P)
    roll = F(a2 * F(P[8]))
    if wobble:
        roll = F(F(F(F(math.pi) * F(math.sin(F(t * F(0.21))))) + roll) + F(F(math.pi) * F(math.sin(F(t * F(0.62))))))
        roll = F(roll + F(math.sin(F(t * F(0.14)))))
    return t, eye, tgt, roll

if __name__ == '__main__':
    print('P mode 0', PSETS[0], 'speed', SPEED[0]); print('P mode 2', PSETS[2], 'speed', SPEED[2])
    for pk, mode in (('p1_hooks.pkl', 0), ('p2_hooks.pkl', 2)):
        rec = pickle.load(open(pk, 'rb'))
        worst = 0; n = 0
        for e in rec['cam']:
            tick, fl, time, tb, vb = e[:5]
            if mode == 2 and tick > 3278: continue      # next part (0x15c8b)
            time = time - (1 << 32) if time >> 31 else time
            tt = struct.unpack('<f', tb)[0]
            v = struct.unpack('<8f', vb)
            wob = (mode == 2) or tick >= 1744
            t, eye, tgt, roll = camera(None, mode, wob, F(tt))
            got = np.array(v[:3] + v[3:6] + (v[7],)); mine = np.array(eye + tgt + (roll,), np.float64)
            err = np.abs(got - mine).max()
            worst = max(worst, err); n += 1
            if err > 0.05: print('  ', pk, tick, time, err, got, mine)
        print(pk, 'frames', n, 'max abs error (eye, target, roll):', worst)
