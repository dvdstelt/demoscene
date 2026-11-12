"""Face part: compare the recorded frames (which face_model.py reproduces) with full.frames, check the palette as
file 35's palette scaled by the fade level, and list the script's phase changes and the strobe frames."""
import pickle, os
import numpy as np
import snap, face_model as fm

h = pickle.load(open('runs/f.pkl', 'rb')); ev = h['events']; fr = h['frames']
raw = np.memmap(os.path.join(snap.RE, 'full.frames'), np.uint8, 'r', 32); REC = 64768
ends = [e[1] for e in ev if e[0] == 'E']
pal = fm.PAL.astype(int)
bad_v = bad_p = 0
for f in range(ends[0], ends[-1]):
    rec = raw[f * REC:(f + 1) * REC]
    k = max(i for i, x in enumerate(ends) if x <= f)
    bad_v += not np.array_equal(rec[:64000], fr[k])
    p = rec[64000:].reshape(256, 3).astype(int)
    bad_p += not any(np.array_equal(np.minimum(63, (pal * L) >> 8), p) for L in range(0, 257, 4))
print('records %d..%d: VGA differs in %d, palette not pal*L>>8 in %d' % (ends[0], ends[-1] - 1, bad_v, bad_p))
# phase changes as the script model sees them
sm = snap.Mem(snap.load('f23436_1089c.snap')['mem'])
sc = fm.Script(sm); ph = sc.phase; n = 0; extra = 0; ticks = 0; t0 = None
names = {0: 'zoom oscillates, no ripple', 1: 'light turns once (0x50/tick)', 2: 'block 0x107dc', 3: '(wait)',
         4: 'light 0x1e/tick', 5: 'block 0x1081c', 6: 'strobe', 7: 'face on', 8: 'block 0x1085c, z ramp',
         9: 'face off, z += 0x16', 10: 'fade out'}
for e in ev:
    if e[0] == 'T':
        sc.tick(); ticks += 1
        if t0 is None: t0 = e[1] - 1
    elif e[0] == 'F':
        sc.before_frame()
    elif e[0] == 'E':
        was_extra = sc.extra
        sc.after_frame(e[2] << 8 | e[3]); n += 1
        if sc.extra: extra += 1
        if sc.phase != ph:
            print('after frame %d (record %d, pos %02x:%02x, PIT tick %d of the part): phase %d -> %d %s; z=%d tab=%d face=%d angle=%#x'
                  % (n, e[1], e[2], e[3], ticks, ph, sc.phase, names.get(sc.phase, ''), sc.p[9], sc.p[10], sc.p[11], sc.angle))
            ph = sc.phase
print('strobe: %d extra frames with the face' % extra, 'final inc', sc.inc)
